/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 * e-shell-window-actions.h
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef E_SHELL_WINDOW_ACTIONS_H
#define E_SHELL_WINDOW_ACTIONS_H

#define E_SHELL_WINDOW_ACTION(window, name) \
	(e_shell_window_get_action (E_SHELL_WINDOW (window), (name)))

#define E_SHELL_WINDOW_ACTION_GROUP(window, name) \
	(e_shell_window_get_action_group (E_SHELL_WINDOW (window), (name)))

/* Actions */
#define E_SHELL_WINDOW_ACTION_ABOUT(window) \
	E_SHELL_WINDOW_ACTION ((window), "about")
#define E_SHELL_WINDOW_ACTION_CLOSE(window) \
	E_SHELL_WINDOW_ACTION ((window), "close")
#define E_SHELL_WINDOW_ACTION_CONTENTS(window) \
	E_SHELL_WINDOW_ACTION ((window), "contents")
#define E_SHELL_WINDOW_ACTION_FAQ(window) \
	E_SHELL_WINDOW_ACTION ((window), "faq")
#define E_SHELL_WINDOW_ACTION_FORGET_PASSWORDS(window) \
	E_SHELL_WINDOW_ACTION ((window), "forget-passwords")
#define E_SHELL_WINDOW_ACTION_GAL_CUSTOM_VIEW(window) \
	E_SHELL_WINDOW_ACTION ((window), "gal-custom-view")
#define E_SHELL_WINDOW_ACTION_GAL_DEFINE_VIEWS(window) \
	E_SHELL_WINDOW_ACTION ((window), "gal-define-views")
#define E_SHELL_WINDOW_ACTION_GAL_SAVE_CUSTOM_VIEW(window) \
	E_SHELL_WINDOW_ACTION ((window), "gal-save-custom-view")
#define E_SHELL_WINDOW_ACTION_IMPORT(window) \
	E_SHELL_WINDOW_ACTION ((window), "import")
#define E_SHELL_WINDOW_ACTION_NEW_WINDOW(window) \
	E_SHELL_WINDOW_ACTION ((window), "new-window")
#define E_SHELL_WINDOW_ACTION_PAGE_SETUP(window) \
	E_SHELL_WINDOW_ACTION ((window), "page-setup")
#define E_SHELL_WINDOW_ACTION_PREFERENCES(window) \
	E_SHELL_WINDOW_ACTION ((window), "preferences")
#define E_SHELL_WINDOW_ACTION_QUICK_REFERENCE(window) \
	E_SHELL_WINDOW_ACTION ((window), "quick-reference")
#define E_SHELL_WINDOW_ACTION_QUIT(window) \
	E_SHELL_WINDOW_ACTION ((window), "quit")
#define E_SHELL_WINDOW_ACTION_SEARCH_ADVANCED(window) \
	E_SHELL_WINDOW_ACTION ((window), "search-advanced")
#define E_SHELL_WINDOW_ACTION_SEARCH_CLEAR(window) \
	E_SHELL_WINDOW_ACTION ((window), "search-clear")
#define E_SHELL_WINDOW_ACTION_SEARCH_EDIT(window) \
	E_SHELL_WINDOW_ACTION ((window), "search-edit")
#define E_SHELL_WINDOW_ACTION_SEARCH_EXECUTE(window) \
	E_SHELL_WINDOW_ACTION ((window), "search-execute")
#define E_SHELL_WINDOW_ACTION_SEARCH_OPTIONS(window) \
	E_SHELL_WINDOW_ACTION ((window), "search-options")
#define E_SHELL_WINDOW_ACTION_SEARCH_SAVE(window) \
	E_SHELL_WINDOW_ACTION ((window), "search-save")
#define E_SHELL_WINDOW_ACTION_SEND_RECEIVE(window) \
	E_SHELL_WINDOW_ACTION ((window), "send-receive")
#define E_SHELL_WINDOW_ACTION_SHOW_SIDEBAR(window) \
	E_SHELL_WINDOW_ACTION ((window), "show-sidebar")
#define E_SHELL_WINDOW_ACTION_SHOW_STATUSBAR(window) \
	E_SHELL_WINDOW_ACTION ((window), "show-statusbar")
#define E_SHELL_WINDOW_ACTION_SHOW_SWITCHER(window) \
	E_SHELL_WINDOW_ACTION ((window), "show-switcher")
#define E_SHELL_WINDOW_ACTION_SHOW_TOOLBAR(window) \
	E_SHELL_WINDOW_ACTION ((window), "show-toolbar")
#define E_SHELL_WINDOW_ACTION_SUBMIT_BUG(window) \
	E_SHELL_WINDOW_ACTION ((window), "submit-bug")
#define E_SHELL_WINDOW_ACTION_SWITCHER_STYLE_BOTH(window) \
	E_SHELL_WINDOW_ACTION ((window), "switcher-style-both")
#define E_SHELL_WINDOW_ACTION_SWITCHER_STYLE_ICONS(window) \
	E_SHELL_WINDOW_ACTION ((window), "switcher-style-icons")
#define E_SHELL_WINDOW_ACTION_SWITCHER_STYLE_TEXT(window) \
	E_SHELL_WINDOW_ACTION ((window), "switcher-style-text")
#define E_SHELL_WINDOW_ACTION_SWITCHER_STYLE_USER(window) \
	E_SHELL_WINDOW_ACTION ((window), "switcher-style-user")
#define E_SHELL_WINDOW_ACTION_SYNC_OPTIONS(window) \
	E_SHELL_WINDOW_ACTION ((window), "sync-options")
#define E_SHELL_WINDOW_ACTION_WORK_OFFLINE(window) \
	E_SHELL_WINDOW_ACTION ((window), "work-offline")
#define E_SHELL_WINDOW_ACTION_WORK_ONLINE(window) \
	E_SHELL_WINDOW_ACTION ((window), "work-online")

/* Action Groups */
#define E_SHELL_WINDOW_ACTION_GROUP_CUSTOM_RULES(window) \
	E_SHELL_WINDOW_ACTION_GROUP ((window), "custom-rules")
#define E_SHELL_WINDOW_ACTION_GROUP_GAL_VIEW(window) \
	E_SHELL_WINDOW_ACTION_GROUP ((window), "gal-view")
#define E_SHELL_WINDOW_ACTION_GROUP_NEW_ITEM(window) \
	E_SHELL_WINDOW_ACTION_GROUP ((window), "new-item")
#define E_SHELL_WINDOW_ACTION_GROUP_NEW_SOURCE(window) \
	E_SHELL_WINDOW_ACTION_GROUP ((window), "new-source")
#define E_SHELL_WINDOW_ACTION_GROUP_SHELL(window) \
	E_SHELL_WINDOW_ACTION_GROUP ((window), "shell")
#define E_SHELL_WINDOW_ACTION_GROUP_SWITCHER(window) \
	E_SHELL_WINDOW_ACTION_GROUP ((window), "switcher")

#endif /* E_SHELL_WINDOW_ACTIONS_H */
