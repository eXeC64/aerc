/*
 * handlers.c - handlers for worker messages
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "log.h"
#include "state.h"
#include "ui.h"
#include "util/list.h"
#include "worker.h"

void handle_worker_connect_done(struct account_state *account,
		struct worker_message *message) {
	worker_post_action(account->worker.pipe, WORKER_LIST, NULL, NULL);
	set_status(account, ACCOUNT_OKAY, "Connected.");
}

void handle_worker_connect_error(struct account_state *account,
		struct worker_message *message) {
	set_status(account, ACCOUNT_ERROR, (char *)message->data);
}

void handle_worker_select_done(struct account_state *account,
		struct worker_message *message) {
	set_status(account, ACCOUNT_OKAY, "Connected.");
	account->ui.list_offset = 0;
	account->selected = strdup((char *)message->data);
	request_rerender(PANEL_MESSAGE_LIST);
}

void handle_worker_select_error(struct account_state *account,
		struct worker_message *message) {
	set_status(account, ACCOUNT_ERROR, "Unable to select that mailbox.");
}

void handle_worker_list_done(struct account_state *account,
		struct worker_message *message) {
	account->mailboxes = message->data;
	char *wanted = "INBOX";
	struct account_config *c = config_for_account(account->name);
	for (size_t i = 0; i < c->extras->length; ++i) {
		struct account_config_extra *extra = c->extras->items[i];
		if (strcmp(extra->key, "default") == 0) {
			wanted = extra->value;
			break;
		}
	}
	bool have_wanted = false;
	for (size_t i = 0; i < account->mailboxes->length; ++i) {
		struct aerc_mailbox *mbox = account->mailboxes->items[i];
		if (strcmp(mbox->name, wanted) == 0) {
			have_wanted = true;
		}
	}
	if (have_wanted) {
		free(account->selected);
		account->selected = strdup(wanted);
		worker_post_action(account->worker.pipe, WORKER_SELECT_MAILBOX,
				NULL, strdup(wanted));
	}
	request_rerender(PANEL_MESSAGE_LIST | PANEL_SIDEBAR);
}

void handle_worker_list_error(struct account_state *account,
		struct worker_message *message) {
	set_status(account, ACCOUNT_ERROR, "Unable to list folders!");
}

void handle_worker_connect_cert_check(struct account_state *account,
		struct worker_message *message) {
#ifdef USE_OPENSSL
	// TODO: interactive certificate check
	worker_post_action(account->worker.pipe, WORKER_CONNECT_CERT_OKAY,
			message, NULL);
#endif
}

void handle_worker_mailbox_updated(struct account_state *account,
		struct worker_message *message) {
	/*
	 * This generally happens when a mailbox is first being initialized, and
	 * when new messages arrive in it.
	 */
	struct aerc_mailbox *new = message->data;
	struct aerc_mailbox *old = NULL;

	worker_log(L_DEBUG, "Updating mailbox on UI thread");
	for (size_t i = 0; i < account->mailboxes->length; ++i) {
		old = account->mailboxes->items[i];
		if (strcmp(old->name, new->name) == 0) {
			account->mailboxes->items[i] = new;
			break;
		}
	}
	free_aerc_mailbox(old);
	request_rerender(PANEL_MESSAGE_LIST | PANEL_SIDEBAR);
}

void handle_worker_message_updated(struct account_state *account,
		struct worker_message *message) {
	worker_log(L_DEBUG, "Updated message on UI thread");
	struct aerc_message_update *update = message->data;
	struct aerc_message *new = update->message;
	struct aerc_mailbox *mbox = get_aerc_mailbox(account, update->mailbox);
	for (size_t i = 0; i < mbox->messages->length; ++i) {
		struct aerc_message *old = mbox->messages->items[i];
		if (old->index == new->index) {
			free_aerc_message(mbox->messages->items[i]);
			new->fetched = true;
			mbox->messages->items[i] = new;
			rerender_item(i);
		}
	}
	free(update->mailbox);
}

void handle_worker_mailbox_deleted(struct account_state *account,
		struct worker_message *message) {
	worker_log(L_DEBUG, "Deleting mailbox on UI thread");
	struct aerc_mailbox *mbox = get_aerc_mailbox(account, (const char *)message->data);
	for (size_t i = 0; i < account->mailboxes->length; ++i) {
		if (account->mailboxes->items[i] == mbox) {
			list_del(account->mailboxes, i);
		}
	}
	free_aerc_mailbox(mbox);
	request_rerender(PANEL_MESSAGE_LIST | PANEL_SIDEBAR);
}
