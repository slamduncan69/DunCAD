#define _GNU_SOURCE
#include "ai_chat.h"
#include "core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct DC_AiChat {
    DC_AiChatResponseCb response_cb;
    void               *response_data;
    DC_AiChatToolCb     tool_cb;
    void               *tool_data;

    char  *system_prompt;
    char  *session_id;   /* set after first call, used with --resume */
    int    busy;
    int    first_call;   /* 1 = first message (use --system-prompt), 0 = --continue */
};

/* Callback when claude subprocess completes. */
static void
on_claude_done(GObject *source, GAsyncResult *result, gpointer data)
{
    DC_AiChat *chat = data;
    GSubprocess *proc = G_SUBPROCESS(source);

    char *stdout_buf = NULL;
    GError *err = NULL;
    g_subprocess_communicate_utf8_finish(proc, result, &stdout_buf, NULL, &err);
    g_object_unref(proc);

    if (err) {
        if (chat->response_cb)
            chat->response_cb(err->message, chat->response_data);
        g_error_free(err);
        g_free(stdout_buf);
        chat->busy = 0;
        return;
    }

    if (stdout_buf && *stdout_buf) {
        if (chat->response_cb) {
            chat->response_cb(stdout_buf, chat->response_data);
            chat->response_cb("\n", chat->response_data);
        }
    } else {
        if (chat->response_cb)
            chat->response_cb("(no response)\n", chat->response_data);
    }

    g_free(stdout_buf);
    chat->busy = 0;
    chat->first_call = 0;
}

static void
do_claude_call(DC_AiChat *chat, const char *message)
{
    GError *err = NULL;
    GSubprocess *proc = NULL;

    /* Use launcher to unset CLAUDECODE env var (prevents nesting check) */
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDIN_PIPE |
        G_SUBPROCESS_FLAGS_STDOUT_PIPE |
        G_SUBPROCESS_FLAGS_STDERR_MERGE);
    g_subprocess_launcher_unsetenv(launcher, "CLAUDECODE");

    if (chat->first_call) {
        proc = g_subprocess_launcher_spawn(launcher, &err,
            "claude",
            "--print",
            "--output-format", "text",
            "--model", "sonnet",
            "--dangerously-skip-permissions",
            "--system-prompt", chat->system_prompt,
            message,
            NULL);
    } else {
        proc = g_subprocess_launcher_spawn(launcher, &err,
            "claude",
            "--print",
            "--output-format", "text",
            "--model", "sonnet",
            "--dangerously-skip-permissions",
            "--continue",
            message,
            NULL);
    }
    g_object_unref(launcher);

    if (!proc) {
        if (chat->response_cb)
            chat->response_cb(err ? err->message : "Failed to spawn claude",
                              chat->response_data);
        if (err) g_error_free(err);
        chat->busy = 0;
        return;
    }

    g_subprocess_communicate_utf8_async(proc, NULL, NULL,
                                         on_claude_done, chat);
}

/* ===== Public interface ===== */

DC_AiChat *
dc_ai_chat_new(void)
{
    /* Check that claude CLI is available */
    const char *path = g_find_program_in_path("claude");
    if (!path) {
        DC_LOG_INFO_APP("ai_chat: claude CLI not found in PATH%s", "");
        return NULL;
    }

    DC_AiChat *chat = calloc(1, sizeof(*chat));
    if (!chat) return NULL;

    chat->first_call = 1;

    /* Build system prompt with DunCAD context */
    chat->system_prompt = strdup(
        "You are the AI assistant embedded in DunCAD, a 3D modeling IDE "
        "built in pure C with GTK4, OpenGL, and Trinity Site (OpenSCAD engine). "
        "You are running inside a terminal panel in the bottom-right of the DunCAD window. "
        "The user is the developer and creator of this software.\n\n"
        "You can manipulate the running DunCAD instance using the duncad-inspect CLI tool "
        "which connects to the app via Unix socket. Examples:\n"
        "  duncad-inspect help          # list all commands\n"
        "  duncad-inspect set_code '...' # set editor code\n"
        "  duncad-inspect preview_render # render the current code\n"
        "  duncad-inspect gl_capture /tmp/shot.png  # screenshot viewport\n"
        "  duncad-inspect get_code_text  # get current editor text\n\n"
        "Other useful tools: talmud (project docs), duncad-docs, cmake.\n"
        "The DunCAD source is at /home/duncan/workspace/coding/DunCAD\n"
        "The talmud project is at /home/duncan/workspace/coding/DunCAD/talmud-main\n\n"
        "Keep responses concise. You have full shell access."
    );

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "ai_chat: initialized (claude CLI)");
    return chat;
}

void
dc_ai_chat_free(DC_AiChat *chat)
{
    if (!chat) return;
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
dc_ai_chat_send(DC_AiChat *chat, const char *message)
{
    if (!chat || !message || chat->busy) return;
    chat->busy = 1;
    do_claude_call(chat, message);
}

int
dc_ai_chat_busy(const DC_AiChat *chat)
{
    return chat ? chat->busy : 0;
}
