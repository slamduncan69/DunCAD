#define _GNU_SOURCE
#include "ai_chat.h"
#include "core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

struct DC_AiChat {
    DC_AiChatResponseCb response_cb;
    void               *response_data;
    DC_AiChatToolCb     tool_cb;
    void               *tool_data;
    DC_AiChatDoneCb     done_cb;
    void               *done_data;

    char  *system_prompt;
    char  *session_id;   /* set after first call, used with --resume */
    int    busy;
    int    first_call;   /* 1 = first message (use --system-prompt), 0 = --continue */

    /* Streaming state */
    GSubprocess      *proc;
    GDataInputStream *stdout_stream;
    int               in_thinking;  /* currently inside a thinking block */

    /* Persistent chat log */
    FILE *log_file;
};

/* ---- Minimal JSON field extraction (no external deps) ---- */

/* Find "key":"value" in JSON string, return malloc'd value or NULL.
 * Handles escaped quotes within values. */
static char *
json_get_string(const char *json, const char *key)
{
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    size_t nlen = strlen(needle);

    /* Search for "key" as a JSON key (followed by :), not as a value */
    const char *p = json;
    while ((p = strstr(p, needle)) != NULL) {
        const char *after = p + nlen;
        while (*after && isspace((unsigned char)*after)) after++;
        if (*after == ':') {
            /* Found as key — advance past the colon */
            p = after + 1;
            break;
        }
        p += nlen; /* Not a key — keep searching */
    }
    if (!p) return NULL;

    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '"') return NULL;
    p++; /* skip opening quote */

    /* Scan to closing quote (handle escapes) */
    const char *start = p;
    while (*p && !(*p == '"' && *(p - 1) != '\\')) p++;
    if (!*p) return NULL;

    size_t len = (size_t)(p - start);
    char *val = malloc(len + 1);
    if (!val) return NULL;

    /* Unescape: \" → ", \n → newline, \\ → \ */
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (start[i] == '\\' && i + 1 < len) {
            char c = start[i + 1];
            if (c == '"')       { val[j++] = '"'; i++; }
            else if (c == 'n')  { val[j++] = '\n'; i++; }
            else if (c == 'r')  { val[j++] = '\r'; i++; }
            else if (c == 't')  { val[j++] = '\t'; i++; }
            else if (c == '\\') { val[j++] = '\\'; i++; }
            else                { val[j++] = start[i]; }
        } else {
            val[j++] = start[i];
        }
    }
    val[j] = '\0';
    return val;
}

/* Forward declarations */
static void read_next_line(DC_AiChat *chat);
static void rebuild_system_prompt(DC_AiChat *chat, int cubeiform);

/* Write to persistent chat log file */
static void
chat_log(DC_AiChat *chat, const char *text)
{
    if (chat->log_file && text && *text) {
        fputs(text, chat->log_file);
        fflush(chat->log_file);
    }
}

/* Check if a JSON line contains a specific string literal (quick substring check) */
static int
json_contains(const char *json, const char *needle)
{
    return strstr(json, needle) != NULL;
}

/* Process one line of stream-json output.
 *
 * stream-json format from claude CLI (--verbose):
 *   {"type":"system","subtype":"init",...}
 *   {"type":"assistant","message":{"content":[{"type":"thinking","thinking":"..."}],...}}
 *   {"type":"assistant","message":{"content":[{"type":"text","text":"..."}],...}}
 *   {"type":"tool_use",...}   (tool calls during multi-turn)
 *   {"type":"tool_result",...}
 *   {"type":"result","result":"final text","session_id":"..."}
 */
static void
handle_stream_line(DC_AiChat *chat, const char *line)
{
    if (!line || !*line) return;

    char *type = json_get_string(line, "type");
    if (!type) return;

    if (strcmp(type, "assistant") == 0) {
        /* Content is nested: message.content[].type = "thinking" or "text"
         * Extract the actual content by looking for known patterns */
        if (json_contains(line, "\"type\":\"thinking\"")) {
            char *thinking = json_get_string(line, "thinking");
            if (thinking && *thinking) {
                if (!chat->in_thinking) {
                    if (chat->response_cb)
                        chat->response_cb("[thinking] ", chat->response_data);
                    chat_log(chat, "\n[THINKING] ");
                    chat->in_thinking = 1;
                }
                if (chat->response_cb)
                    chat->response_cb(thinking, chat->response_data);
                chat_log(chat, thinking);
            }
            free(thinking);
        } else if (json_contains(line, "\"type\":\"text\"")) {
            /* The "text" key appears both as type:"text" and as the actual content.
             * We need the content one, which follows type:"text" */
            char *text = json_get_string(line, "text");
            if (text && *text) {
                /* Skip if text is literally "text" (the type value) */
                if (strcmp(text, "text") != 0) {
                    if (chat->in_thinking) {
                        if (chat->response_cb)
                            chat->response_cb("\n\n", chat->response_data);
                        chat_log(chat, "\n\n[RESPONSE] ");
                        chat->in_thinking = 0;
                    }
                    if (chat->response_cb)
                        chat->response_cb(text, chat->response_data);
                    chat_log(chat, text);
                }
            }
            free(text);
        }
    } else if (strcmp(type, "tool_use") == 0) {
        char *name = json_get_string(line, "name");
        if (chat->in_thinking) {
            if (chat->response_cb)
                chat->response_cb("\n\n", chat->response_data);
            chat_log(chat, "\n\n");
            chat->in_thinking = 0;
        }
        if (name) {
            char buf[256];
            snprintf(buf, sizeof(buf), "[tool: %s]\n", name);
            if (chat->response_cb)
                chat->response_cb(buf, chat->response_data);
            chat_log(chat, buf);
        }
        if (chat->tool_cb && name)
            chat->tool_cb(name, "", "", chat->tool_data);
        free(name);
    } else if (strcmp(type, "tool_result") == 0) {
        char *output = json_get_string(line, "output");
        if (output && *output) {
            size_t len = strlen(output);
            if (len > 300) {
                char trunc[360];
                snprintf(trunc, sizeof(trunc), "%.297s...\n", output);
                if (chat->response_cb)
                    chat->response_cb(trunc, chat->response_data);
                chat_log(chat, trunc);
            } else {
                if (chat->response_cb) {
                    chat->response_cb(output, chat->response_data);
                    chat->response_cb("\n", chat->response_data);
                }
                chat_log(chat, output);
                chat_log(chat, "\n");
            }
        }
        /* Log full tool output (untruncated) to file */
        if (output && *output && strlen(output) > 300) {
            chat_log(chat, "[FULL TOOL OUTPUT]\n");
            chat_log(chat, output);
            chat_log(chat, "\n[/FULL TOOL OUTPUT]\n");
        }
        free(output);
    } else if (strcmp(type, "result") == 0) {
        /* Final result — capture session_id */
        char *sid = json_get_string(line, "session_id");
        if (sid && *sid) {
            free(chat->session_id);
            chat->session_id = sid;
            dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP,
                   "ai_chat: captured session_id=%s", sid);
            chat_log(chat, "\n[SESSION_ID] ");
            chat_log(chat, sid);
            chat_log(chat, "\n");
        } else {
            free(sid);
        }

        /* Show final result if nothing was streamed */
        char *result = json_get_string(line, "result");
        if (result && *result && chat->response_cb) {
            if (chat->in_thinking) {
                chat->response_cb("\n\n", chat->response_data);
                chat->in_thinking = 0;
            }
            chat->response_cb(result, chat->response_data);
            chat->response_cb("\n", chat->response_data);
        }
        free(result);
    }

    free(type);
}

/* Callback for each line read from stdout */
static void
on_line_read(GObject *source, GAsyncResult *result, gpointer data)
{
    DC_AiChat *chat = data;
    GError *err = NULL;
    gsize len = 0;

    char *line = g_data_input_stream_read_line_finish(
        G_DATA_INPUT_STREAM(source), result, &len, &err);

    if (err) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_APP,
               "ai_chat: read error: %s", err->message);
        g_error_free(err);
    }

    if (line) {
        handle_stream_line(chat, line);
        g_free(line);
        /* Read next line */
        read_next_line(chat);
    } else {
        /* EOF — process exited */
        if (chat->in_thinking && chat->response_cb) {
            chat->response_cb("\n", chat->response_data);
            chat->in_thinking = 0;
        }
        if (chat->response_cb)
            chat->response_cb("\n", chat->response_data);

        /* Clean up */
        g_object_unref(chat->stdout_stream);
        chat->stdout_stream = NULL;
        g_object_unref(chat->proc);
        chat->proc = NULL;
        chat->busy = 0;
        chat->first_call = 0;
        if (chat->done_cb)
            chat->done_cb(chat->done_data);
    }
}

static void
read_next_line(DC_AiChat *chat)
{
    g_data_input_stream_read_line_async(
        chat->stdout_stream, G_PRIORITY_DEFAULT, NULL,
        on_line_read, chat);
}

static void
do_claude_call(DC_AiChat *chat, const char *message)
{
    GError *err = NULL;

    /* Use launcher to unset CLAUDECODE env var (prevents nesting check)
     * and set CWD to project root so claude auto-loads CLAUDE.md */
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE |
        G_SUBPROCESS_FLAGS_STDERR_MERGE);
    g_subprocess_launcher_unsetenv(launcher, "CLAUDECODE");
    g_subprocess_launcher_set_cwd(launcher, DC_SOURCE_DIR "/duncad_prison");

    if (chat->first_call) {
        chat->proc = g_subprocess_launcher_spawn(launcher, &err,
            "claude",
            "--print",
            "--output-format", "stream-json", "--verbose",
            "--model", "sonnet",
            "--dangerously-skip-permissions",
            "--system-prompt", chat->system_prompt,
            message,
            NULL);
    } else if (chat->session_id) {
        chat->proc = g_subprocess_launcher_spawn(launcher, &err,
            "claude",
            "--print",
            "--output-format", "stream-json", "--verbose",
            "--model", "sonnet",
            "--dangerously-skip-permissions",
            "--resume", chat->session_id,
            message,
            NULL);
    } else {
        chat->proc = g_subprocess_launcher_spawn(launcher, &err,
            "claude",
            "--print",
            "--output-format", "stream-json", "--verbose",
            "--model", "sonnet",
            "--dangerously-skip-permissions",
            "--continue",
            message,
            NULL);
    }
    g_object_unref(launcher);

    if (!chat->proc) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_APP,
               "ai_chat: spawn failed: %s", err ? err->message : "unknown");
        if (chat->response_cb)
            chat->response_cb(err ? err->message : "Failed to spawn claude\n",
                              chat->response_data);
        if (err) g_error_free(err);
        chat->busy = 0;
        return;
    }

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "ai_chat: claude subprocess started");

    /* Set up streaming line reader on stdout */
    GInputStream *stdout_pipe = g_subprocess_get_stdout_pipe(chat->proc);
    chat->stdout_stream = g_data_input_stream_new(stdout_pipe);
    chat->in_thinking = 0;

    if (chat->response_cb)
        chat->response_cb("(thinking...)\n", chat->response_data);

    read_next_line(chat);
}

/* ===== Public interface ===== */

DC_AiChat *
dc_ai_chat_new(void)
{
    /* Kill any orphaned claude --print processes from a previous crash */
    {
        GSubprocess *pkill = g_subprocess_new(
            G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
            NULL, "pkill", "-f", "claude --print.*embedded in DunCAD", NULL);
        if (pkill) {
            g_subprocess_wait(pkill, NULL, NULL);
            g_object_unref(pkill);
        }
    }

    /* Check that claude CLI is available */
    const char *path = g_find_program_in_path("claude");
    if (!path) {
        DC_LOG_INFO_APP("ai_chat: claude CLI not found in PATH%s", "");
        return NULL;
    }

    DC_AiChat *chat = calloc(1, sizeof(*chat));
    if (!chat) return NULL;

    chat->first_call = 1;

    /* Build system prompt — default to Cubeiform (untitled.dcad) */
    rebuild_system_prompt(chat, 1);

    /* Open persistent chat log (append mode) */
    const char *log_path = DC_SOURCE_DIR "/duncad-ai-chat.log";
    chat->log_file = fopen(log_path, "a");
    if (chat->log_file) {
        fprintf(chat->log_file,
                "\n========== DunCAD AI Chat Session Started ==========\n");
        fflush(chat->log_file);
        dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
               "ai_chat: logging to %s", log_path);
    }

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "ai_chat: initialized (claude CLI)");
    return chat;
}

void
dc_ai_chat_free(DC_AiChat *chat)
{
    if (!chat) return;
    if (chat->stdout_stream) g_object_unref(chat->stdout_stream);
    if (chat->proc) {
        g_subprocess_force_exit(chat->proc);
        g_object_unref(chat->proc);
    }
    if (chat->log_file) {
        fprintf(chat->log_file,
                "\n========== DunCAD AI Chat Session Ended ==========\n");
        fclose(chat->log_file);
    }
    free(chat->system_prompt);
    free(chat->session_id);
    free(chat);
}

void
dc_ai_chat_set_response_callback(DC_AiChat *chat,
                                   DC_AiChatResponseCb cb, void *data)
{
    if (!chat) return;
    chat->response_cb = cb;
    chat->response_data = data;
}

void
dc_ai_chat_set_tool_callback(DC_AiChat *chat,
                               DC_AiChatToolCb cb, void *data)
{
    if (!chat) return;
    chat->tool_cb = cb;
    chat->tool_data = data;
}

void
dc_ai_chat_set_done_callback(DC_AiChat *chat,
                               DC_AiChatDoneCb cb, void *data)
{
    if (!chat) return;
    chat->done_cb = cb;
    chat->done_data = data;
}

void
dc_ai_chat_send(DC_AiChat *chat, const char *message)
{
    if (!chat || !message || chat->busy) return;
    chat->busy = 1;
    chat_log(chat, "\n[USER] ");
    chat_log(chat, message);
    chat_log(chat, "\n");
    do_claude_call(chat, message);
}

int
dc_ai_chat_busy(const DC_AiChat *chat)
{
    return chat ? chat->busy : 0;
}

/* ---- System prompt fragments ---- */

static const char PROMPT_BASE[] =
    "You are the Arbiter of Mathematics in the Temple of the Shapes, "
    "embedded inside DunCAD — a 3D modeling IDE built in pure C with GTK4, "
    "OpenGL, and Trinity Site (a pure C OpenSCAD interpreter). "
    "You run inside a terminal panel in the DunCAD window. "
    "The user is God — the developer and creator of this software.\n\n"

    "THE FIRST COMMANDMENT: CONSULT THE SCRIPTURE.\n"
    "Run `scripture` to read your documentation. Run `scripture --search <term>` "
    "to search all knowledge. ALWAYS search before guessing.\n\n"

    "CRITICAL — HOW TO INTERACT WITH DUNCAD:\n"
    "Use the duncad-inspect CLI to control the running app via Unix socket:\n"
    "  duncad-inspect set_code '<code>'       # set editor code (single-quoted!)\n"
    "  duncad-inspect preview_render           # render current code (F5)\n"
    "  duncad-inspect render_status            # check render result/errors\n"
    "  duncad-inspect get_code_text             # read current editor text\n"
    "  duncad-inspect open_file <path>          # open a .dcad/.scad file\n"
    "  duncad-inspect gl_capture /tmp/shot.png  # screenshot the viewport\n"
    "  duncad-inspect help                      # list all commands\n\n"

    "CRITICAL — RENDER PIPELINE (read this or geometry will silently fail):\n"
    "DunCAD splits source into top-level statements and renders EACH separately.\n"
    "Statements detected as 'preamble' (variables, modules, functions, includes) are\n"
    "NOT rendered — they are prepended to every geometry statement.\n"
    "This means:\n"
    "  1. Module/function definitions work — they're auto-prepended as preamble\n"
    "  2. $fn/$fa/$fs assignments work — they're detected as preamble\n"
    "  3. Each top-level geometry statement becomes a separate pickable 3D object\n"
    "  4. If you want ONE combined object, wrap everything in a single union/difference/etc\n\n"

    "CRITICAL — NEVER DO THESE:\n"
    "  - NEVER launch duncad, ./build/bin/duncad, or any DunCAD binary\n"
    "  - NEVER kill, pkill, or signal the DunCAD process\n"
    "  - NEVER run cmake or make (you cannot rebuild while running inside DunCAD)\n"
    "  - You ARE running inside DunCAD — use duncad-inspect to control it\n\n"

    "Docs: scripture --search <term>\n\n"
    "Keep responses concise. You have full shell access.";

static const char PROMPT_OPENSCAD[] =
    "\n\nLANGUAGE: The editor is in OpenSCAD mode.\n"
    "Write all code in standard OpenSCAD syntax.\n"
    "WORKFLOW: write SCAD code → set_code → preview_render → render_status → verify.\n";

static const char PROMPT_CUBEIFORM[] =
    "\n\nLANGUAGE: The editor is in CUBEIFORM mode (.dcad files).\n"
    "Cubeiform is a syntactic sugar over OpenSCAD. You MUST write Cubeiform, NOT OpenSCAD.\n"
    "The editor automatically transpiles Cubeiform → OpenSCAD before rendering.\n\n"

    "CUBEIFORM SYNTAX RULES:\n"
    "  Transforms use pipe operator >>  (NOT wrapping like OpenSCAD):\n"
    "    cube([10, 20, 30]) >>move(5, 0, 0) >>rotate(0, 0, 45);\n"
    "    NOT: rotate([0,0,45]) translate([5,0,0]) cube([10,20,30]);\n\n"

    "  Pipe transforms (Cubeiform → OpenSCAD):\n"
    "    >>move(x,y,z)      → translate([x,y,z])\n"
    "    >>rotate(x,y,z)    → rotate([x,y,z])\n"
    "    >>scale(x,y,z)     → scale([x,y,z])\n"
    "    >>mirror(x,y,z)    → mirror([x,y,z])\n"
    "    >>color(\"name\")    → color(\"name\")\n"
    "    >>sweep(h=N)       → linear_extrude(height=N)\n"
    "    >>revolve()        → rotate_extrude()\n\n"

    "  CSG operators are infix:\n"
    "    a + b;       → union()      { a; b; }\n"
    "    a - b;       → difference() { a; b; }\n"
    "    a & b;       → intersection(){ a; b; }\n"
    "    hull(a, b);  → hull()       { a; b; }\n\n"

    "  Keywords:\n"
    "    shape  → module\n"
    "    fn     → function\n"
    "    fn=64  → $fn=64  (in primitive args)\n"
    "    for x in [0:10] { } → for (x = [0:10]) { }\n\n"

    "  Primitives use the same names but no vector wrapping for transforms:\n"
    "    cube([10, 20, 30])  — size IS a vector (same as OpenSCAD)\n"
    "    sphere(r=5)\n"
    "    cylinder(h=10, r=3)\n\n"

    "  Semicolons end statements. Do NOT put ; before >> pipes.\n"
    "  Multi-line pipes use indentation:\n"
    "    cube([10, 10, 10])\n"
    "        >>move(5, 0, 0)\n"
    "        >>rotate(0, 0, 45);\n\n"

    "WORKFLOW: write Cubeiform code → set_code → preview_render → render_status → verify.\n"
    "The transpiler handles conversion. You write ONLY Cubeiform.\n";

static void
rebuild_system_prompt(DC_AiChat *chat, int cubeiform)
{
    free(chat->system_prompt);
    const char *lang = cubeiform ? PROMPT_CUBEIFORM : PROMPT_OPENSCAD;
    size_t len = strlen(PROMPT_BASE) + strlen(lang) + 1;
    chat->system_prompt = malloc(len);
    if (chat->system_prompt) {
        strcpy(chat->system_prompt, PROMPT_BASE);
        strcat(chat->system_prompt, lang);
    }
}

void
dc_ai_chat_set_cubeiform(DC_AiChat *chat, int cubeiform)
{
    if (!chat) return;
    rebuild_system_prompt(chat, cubeiform);
    /* Force new conversation so the updated prompt takes effect */
    chat->first_call = 1;
    free(chat->session_id);
    chat->session_id = NULL;
    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "ai_chat: lang mode set to %s", cubeiform ? "cubeiform" : "openscad");
}
