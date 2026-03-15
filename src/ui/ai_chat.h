#ifndef DC_AI_CHAT_H
#define DC_AI_CHAT_H

#include <gtk/gtk.h>

typedef struct DC_AiChat DC_AiChat;

/* Callback for text responses (may be called multiple times per exchange). */
typedef void (*DC_AiChatResponseCb)(const char *text, void *userdata);

/* Callback for tool execution visibility. */
typedef void (*DC_AiChatToolCb)(const char *tool, const char *input,
                                 const char *result, void *userdata);

DC_AiChat *dc_ai_chat_new(void);
void       dc_ai_chat_free(DC_AiChat *chat);

void dc_ai_chat_set_response_callback(DC_AiChat *chat,
                                       DC_AiChatResponseCb cb, void *data);
void dc_ai_chat_set_tool_callback(DC_AiChat *chat,
                                   DC_AiChatToolCb cb, void *data);

/* Callback when AI finishes (process exits). */
typedef void (*DC_AiChatDoneCb)(void *userdata);
void dc_ai_chat_set_done_callback(DC_AiChat *chat,
                                   DC_AiChatDoneCb cb, void *data);

/* Send a user message (async — responses arrive via callbacks). */
void dc_ai_chat_send(DC_AiChat *chat, const char *message);

/* Returns TRUE if a request is in flight. */
int dc_ai_chat_busy(const DC_AiChat *chat);

/* Set language mode: 1 = Cubeiform, 0 = OpenSCAD.
 * Adjusts the system prompt so the AI responds in the correct language.
 * Takes effect on the next new conversation (first_call). */
void dc_ai_chat_set_cubeiform(DC_AiChat *chat, int cubeiform);

#endif /* DC_AI_CHAT_H */
