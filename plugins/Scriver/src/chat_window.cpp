/*
Chat module plugin for Miranda IM

Copyright (C) 2003 Jörgen Persson
Copyright 2003-2009 Miranda ICQ/IM project,

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "stdafx.h"

void CMsgDialog::TabAutoComplete()
{
	LRESULT lResult = (LRESULT)m_message.SendMsg(EM_GETSEL, 0, 0);
	int start = LOWORD(lResult), end = HIWORD(lResult);
	m_message.SendMsg(EM_SETSEL, end, end);

	int iLen = m_message.GetRichTextLength(1200);
	if (iLen <= 0)
		return;

	bool isTopic = false, isRoom = false;
	wchar_t *pszName = nullptr;
	wchar_t* pszText = (wchar_t*)mir_alloc(iLen + 100 * sizeof(wchar_t));

	GETTEXTEX gt = {};
	gt.codepage = 1200;
	gt.cb = iLen + 99 * sizeof(wchar_t);
	gt.flags = GT_DEFAULT;
	m_message.SendMsg(EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)pszText);

	if (start > 1 && pszText[start - 1] == ' ' && pszText[start - 2] == ':')
		start -= 2;

	if (m_wszSearchResult != nullptr) {
		int cbResult = (int)mir_wstrlen(m_wszSearchResult);
		if (start >= cbResult && !wcsnicmp(m_wszSearchResult, pszText + start - cbResult, cbResult)) {
			start -= cbResult;
			goto LBL_SkipEnd;
		}
	}

	while (start > 0 && pszText[start - 1] != ' ' && pszText[start - 1] != 13 && pszText[start - 1] != VK_TAB)
		start--;

LBL_SkipEnd:
	while (end < iLen && pszText[end] != ' ' && pszText[end] != 13 && pszText[end - 1] != VK_TAB)
		end++;

	if (pszText[start] == '#')
		isRoom = true;
	else {
		int topicStart = start;
		while (topicStart >0 && (pszText[topicStart - 1] == ' ' || pszText[topicStart - 1] == 13 || pszText[topicStart - 1] == VK_TAB))
			topicStart--;
		if (topicStart > 5 && wcsstr(&pszText[topicStart - 6], L"/topic") == &pszText[topicStart - 6])
			isTopic = true;
	}

	if (m_wszSearchQuery == nullptr) {
		m_wszSearchQuery = (wchar_t*)mir_alloc(sizeof(wchar_t)*(end - start + 1));
		mir_wstrncpy(m_wszSearchQuery, pszText + start, end - start + 1);
		m_wszSearchResult = mir_wstrdup(m_wszSearchQuery);
		m_pLastSession = nullptr;
	}

	if (isTopic)
		pszName = m_si->ptszTopic;
	else if (isRoom) {
		m_pLastSession = SM_FindSessionAutoComplete(m_si->pszModule, m_si, m_pLastSession, m_wszSearchQuery, m_wszSearchResult);
		if (m_pLastSession != nullptr)
			pszName = m_pLastSession->ptszName;
	}
	else pszName = g_chatApi.UM_FindUserAutoComplete(m_si, m_wszSearchQuery, m_wszSearchResult);

	mir_free(pszText);
	replaceStrW(m_wszSearchResult, nullptr);

	if (pszName == nullptr) {
		if (end != start) {
			m_message.SendMsg(EM_SETSEL, start, end);
			m_message.SendMsg(EM_REPLACESEL, FALSE, (LPARAM)m_wszSearchQuery);
		}
		replaceStrW(m_wszSearchQuery, nullptr);
	}
	else {
		m_wszSearchResult = mir_wstrdup(pszName);
		if (end != start) {
			ptrW szReplace;
			if (!isRoom && !isTopic && g_Settings.bAddColonToAutoComplete && start == 0) {
				szReplace = (wchar_t*)mir_alloc((mir_wstrlen(pszName) + 4) * sizeof(wchar_t));
				mir_wstrcpy(szReplace, pszName);
				mir_wstrcat(szReplace, L": ");
				pszName = szReplace;
			}
			m_message.SendMsg(EM_SETSEL, start, end);
			m_message.SendMsg(EM_REPLACESEL, FALSE, (LPARAM)pszName);
		}
	}
}

void CMsgDialog::FixTabIcons()
{
	HICON hIcon;
	if (!(m_si->wState & GC_EVENT_HIGHLIGHT)) {
		if (m_si->wState & STATE_TALK)
			hIcon = (m_si->wStatus == ID_STATUS_ONLINE) ? m_si->pMI->hOnlineTalkIcon : m_si->pMI->hOfflineTalkIcon;
		else
			hIcon = (m_si->wStatus == ID_STATUS_ONLINE) ? m_si->pMI->hOnlineIcon : m_si->pMI->hOfflineIcon;
	}
	else hIcon = g_dat.hMsgIcon;

	TabControlData tcd = {};
	tcd.iFlags = TCDF_ICON;
	tcd.hIcon = hIcon;
	SendMessage(m_hwndParent, CM_UPDATETABCONTROL, (WPARAM)&tcd, (LPARAM)m_hwnd);
}

/////////////////////////////////////////////////////////////////////////////////////////

void CMsgDialog::onClick_ShowList(CCtrlButton *pButton)
{
	if (!pButton->Enabled() || m_si->iType == GCW_SERVER)
		return;

	m_bNicklistEnabled = !m_bNicklistEnabled;
	pButton->SendMsg(BM_SETIMAGE, IMAGE_ICON, (LPARAM)g_plugin.getIcon(m_bNicklistEnabled ? IDI_NICKLIST : IDI_NICKLIST2));
	ScrollToBottom();
	Resize();
}

void CMsgDialog::onClick_Filter(CCtrlButton *pButton)
{
	if (!pButton->Enabled())
		return;

	m_bFilterEnabled = !m_bFilterEnabled;
	pButton->SendMsg(BM_SETIMAGE, IMAGE_ICON, (LPARAM)g_plugin.getIcon(m_bFilterEnabled ? IDI_FILTER : IDI_FILTER2));
	if (m_bFilterEnabled && db_get_b(0, CHAT_MODULE, "RightClickFilter", 0) == 0)
		ShowFilterMenu();
	else
		RedrawLog();
}

/////////////////////////////////////////////////////////////////////////////////////////

static void __cdecl phase2(SESSION_INFO *si)
{
	Thread_SetName("Scriver: phase2");

	Sleep(30);
	if (si && si->pDlg)
		si->pDlg->RedrawLog2();
}

void CMsgDialog::RedrawLog()
{
	m_si->LastTime = 0;
	if (m_si->pLog) {
		LOGINFO *pLog = m_si->pLog;
		if (m_si->iEventCount > 60) {
			int index = 0;
			while (index < 59) {
				if (pLog->next == nullptr)
					break;

				pLog = pLog->next;
				if ((m_si->iType != GCW_CHATROOM && m_si->iType != GCW_PRIVMESS) || !m_bFilterEnabled || (m_iLogFilterFlags & pLog->iType) != 0)
					index++;
			}
			StreamInEvents(pLog, true);
			mir_forkThread<SESSION_INFO>(phase2, m_si);
		}
		else StreamInEvents(m_si->pLogEnd, true);
	}
	else ClearLog();
}

void CMsgDialog::ShowFilterMenu()
{
	HWND hwnd = CreateDialogParam(g_plugin.getInst(), MAKEINTRESOURCE(IDD_FILTER), m_hwnd, FilterWndProc, (LPARAM)this);
	TranslateDialogDefault(hwnd);

	RECT rc;
	GetWindowRect(m_btnFilter.GetHwnd(), &rc);
	SetWindowPos(hwnd, HWND_TOP, rc.left - 85, (IsWindowVisible(m_btnFilter.GetHwnd()) || IsWindowVisible(m_btnBold.GetHwnd())) ? rc.top - 206 : rc.top - 186, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
}

void CMsgDialog::UpdateNickList()
{
	m_nickList.SendMsg(WM_SETREDRAW, FALSE, 0);
	m_nickList.SendMsg(LB_RESETCONTENT, 0, 0);
	for (auto &ui : m_si->getUserList()) {
		char szIndicator = SM_GetStatusIndicator(m_si, ui);
		if (szIndicator > '\0') {
			static wchar_t ptszBuf[128];
			mir_snwprintf(ptszBuf, L"%c%s", szIndicator, ui->pszNick);
			m_nickList.SendMsg(LB_ADDSTRING, 0, (LPARAM)ptszBuf);
		}
		else m_nickList.SendMsg(LB_ADDSTRING, 0, (LPARAM)ui->pszNick);
	}
	m_nickList.SendMsg(WM_SETREDRAW, TRUE, 0);
	InvalidateRect(m_nickList.GetHwnd(), nullptr, FALSE);
	UpdateWindow(m_nickList.GetHwnd());
	UpdateTitle();
}

void CMsgDialog::UpdateOptions()
{
	m_btnNickList.SendMsg(BM_SETIMAGE, IMAGE_ICON, (LPARAM)g_plugin.getIcon(m_bNicklistEnabled ? IDI_NICKLIST : IDI_NICKLIST2));
	m_btnFilter.SendMsg(BM_SETIMAGE, IMAGE_ICON, (LPARAM)g_plugin.getIcon(m_bFilterEnabled ? IDI_FILTER : IDI_FILTER2));

	m_btnBold.Enable(m_si->pMI->bBold);
	m_btnItalic.Enable(m_si->pMI->bItalics);
	m_btnUnderline.Enable(m_si->pMI->bUnderline);
	m_btnColor.Enable(m_si->pMI->bColor);
	m_btnBkColor.Enable(m_si->pMI->bBkgColor);
	if (m_si->iType == GCW_CHATROOM)
		m_btnChannelMgr.Enable(m_si->pMI->bChanMgr);

	UpdateStatusBar();
	UpdateTitle();
	FixTabIcons();

	m_log.SendMsg(EM_SETBKGNDCOLOR, 0, g_Settings.crLogBackground);

	// messagebox
	COLORREF crFore;
	LoadMsgDlgFont(MSGFONTID_MESSAGEAREA, nullptr, &crFore);

	CHARFORMAT2 cf;
	cf.cbSize = sizeof(CHARFORMAT2);
	cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_UNDERLINE | CFM_BACKCOLOR;
	cf.dwEffects = 0;
	cf.crTextColor = crFore;
	cf.crBackColor = g_plugin.getDword(SRMSGSET_INPUTBKGCOLOUR, SRMSGDEFSET_INPUTBKGCOLOUR);
	m_message.SendMsg(EM_SETBKGNDCOLOR, 0, g_plugin.getDword(SRMSGSET_INPUTBKGCOLOUR, SRMSGDEFSET_INPUTBKGCOLOUR));
	m_message.SendMsg(WM_SETFONT, (WPARAM)g_Settings.MessageBoxFont, MAKELPARAM(TRUE, 0));
	m_message.SendMsg(EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
	{
		// nicklist
		int ih = Chat_GetTextPixelSize(L"AQG_glo'", g_Settings.UserListFont, false);
		int ih2 = Chat_GetTextPixelSize(L"AQG_glo'", g_Settings.UserListHeadingsFont, false);
		int height = db_get_b(0, CHAT_MODULE, "NicklistRowDist", 12);
		int font = ih > ih2 ? ih : ih2;
		// make sure we have space for icon!
		if (db_get_b(0, CHAT_MODULE, "ShowContactStatus", 0))
			font = font > 16 ? font : 16;

		m_nickList.SendMsg(LB_SETITEMHEIGHT, 0, height > font ? height : font);
		InvalidateRect(m_nickList.GetHwnd(), nullptr, TRUE);
	}
	m_message.SendMsg(EM_REQUESTRESIZE, 0, 0);
	Resize();
	RedrawLog2();
}

/////////////////////////////////////////////////////////////////////////////////////////

INT_PTR CALLBACK CMsgDialog::FilterWndProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static CMsgDialog *pDlg = nullptr;
	switch (uMsg) {
	case WM_INITDIALOG:
		pDlg = (CMsgDialog*)lParam;
		CheckDlgButton(hwndDlg, IDC_CHAT_1, pDlg->m_iLogFilterFlags & GC_EVENT_ACTION ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_CHAT_2, pDlg->m_iLogFilterFlags & GC_EVENT_MESSAGE ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_CHAT_3, pDlg->m_iLogFilterFlags & GC_EVENT_NICK ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_CHAT_4, pDlg->m_iLogFilterFlags & GC_EVENT_JOIN ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_CHAT_5, pDlg->m_iLogFilterFlags & GC_EVENT_PART ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_CHAT_6, pDlg->m_iLogFilterFlags & GC_EVENT_TOPIC ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_CHAT_7, pDlg->m_iLogFilterFlags & GC_EVENT_ADDSTATUS ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_CHAT_8, pDlg->m_iLogFilterFlags & GC_EVENT_INFORMATION ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_CHAT_9, pDlg->m_iLogFilterFlags & GC_EVENT_QUIT ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_CHAT_10, pDlg->m_iLogFilterFlags & GC_EVENT_KICK ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_CHAT_11, pDlg->m_iLogFilterFlags & GC_EVENT_NOTICE ? BST_CHECKED : BST_UNCHECKED);
		break;

	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORSTATIC:
		SetTextColor((HDC)wParam, RGB(60, 60, 150));
		SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
		return (INT_PTR)GetSysColorBrush(COLOR_WINDOW);

	case WM_ACTIVATE:
		if (LOWORD(wParam) == WA_INACTIVE) {
			int iFlags = 0;

			if (IsDlgButtonChecked(hwndDlg, IDC_CHAT_1) == BST_CHECKED)
				iFlags |= GC_EVENT_ACTION;
			if (IsDlgButtonChecked(hwndDlg, IDC_CHAT_2) == BST_CHECKED)
				iFlags |= GC_EVENT_MESSAGE;
			if (IsDlgButtonChecked(hwndDlg, IDC_CHAT_3) == BST_CHECKED)
				iFlags |= GC_EVENT_NICK;
			if (IsDlgButtonChecked(hwndDlg, IDC_CHAT_4) == BST_CHECKED)
				iFlags |= GC_EVENT_JOIN;
			if (IsDlgButtonChecked(hwndDlg, IDC_CHAT_5) == BST_CHECKED)
				iFlags |= GC_EVENT_PART;
			if (IsDlgButtonChecked(hwndDlg, IDC_CHAT_6) == BST_CHECKED)
				iFlags |= GC_EVENT_TOPIC;
			if (IsDlgButtonChecked(hwndDlg, IDC_CHAT_7) == BST_CHECKED)
				iFlags |= GC_EVENT_ADDSTATUS;
			if (IsDlgButtonChecked(hwndDlg, IDC_CHAT_8) == BST_CHECKED)
				iFlags |= GC_EVENT_INFORMATION;
			if (IsDlgButtonChecked(hwndDlg, IDC_CHAT_9) == BST_CHECKED)
				iFlags |= GC_EVENT_QUIT;
			if (IsDlgButtonChecked(hwndDlg, IDC_CHAT_10) == BST_CHECKED)
				iFlags |= GC_EVENT_KICK;
			if (IsDlgButtonChecked(hwndDlg, IDC_CHAT_11) == BST_CHECKED)
				iFlags |= GC_EVENT_NOTICE;

			if (iFlags & GC_EVENT_ADDSTATUS)
				iFlags |= GC_EVENT_REMOVESTATUS;

			pDlg->m_iLogFilterFlags = iFlags;
			if (pDlg->m_bFilterEnabled)
				pDlg->RedrawLog();
			PostMessage(hwndDlg, WM_CLOSE, 0, 0);
		}
		break;

	case WM_CLOSE:
		DestroyWindow(hwndDlg);
		break;
	}

	return FALSE;
}

/////////////////////////////////////////////////////////////////////////////////////////

LRESULT CMsgDialog::WndProc_Nicklist(UINT msg, WPARAM wParam, LPARAM lParam)
{
	int result = InputAreaShortcuts(m_nickList.GetHwnd(), msg, wParam, lParam);
	if (result != -1)
		return result;

	switch (msg) {
	case WM_GETDLGCODE:
		{
			BOOL isAlt = GetKeyState(VK_MENU) & 0x8000;
			BOOL isCtrl = (GetKeyState(VK_CONTROL) & 0x8000) && !isAlt;

			LPMSG lpmsg;
			if ((lpmsg = (LPMSG)lParam) != nullptr) {
				if (lpmsg->message == WM_KEYDOWN
					&& (lpmsg->wParam == VK_RETURN || lpmsg->wParam == VK_ESCAPE || (lpmsg->wParam == VK_TAB && (isAlt || isCtrl))))
					return DLGC_WANTALLKEYS;
			}
		}
		break;

	case WM_KEYDOWN:
		if (wParam == VK_RETURN) {
			int index = m_nickList.SendMsg(LB_GETCURSEL, 0, 0);
			if (index != LB_ERR) {
				USERINFO *ui = g_chatApi.SM_GetUserFromIndex(m_si->ptszID, m_si->pszModule, index);
				Chat_DoEventHook(m_si, GC_USER_PRIVMESS, ui, nullptr, 0);
			}
			break;
		}
		
		if (wParam == VK_ESCAPE || wParam == VK_UP || wParam == VK_DOWN || wParam == VK_NEXT || wParam == VK_PRIOR || wParam == VK_TAB || wParam == VK_HOME || wParam == VK_END)
			m_wszSearch[0] = 0;
		break;

	case WM_CHAR:
	case WM_UNICHAR:
		/*
		* simple incremental search for the user (nick) - list control
		* typing esc or movement keys will clear the current search string
		*/
		if (wParam == 27 && m_wszSearch[0]) {						// escape - reset everything
			m_wszSearch[0] = 0;
			break;
		}
		else if (wParam == '\b' && m_wszSearch[0])					// backspace
			m_wszSearch[mir_wstrlen(m_wszSearch) - 1] = '\0';
		else if (wParam < ' ')
			break;
		else {
			wchar_t szNew[2];
			szNew[0] = (wchar_t)wParam;
			szNew[1] = '\0';
			if (mir_wstrlen(m_wszSearch) >= _countof(m_wszSearch) - 2) {
				MessageBeep(MB_OK);
				break;
			}
			mir_wstrcat(m_wszSearch, szNew);
		}
		if (m_wszSearch[0]) {
			// iterate over the (sorted) list of nicknames and search for the
			// string we have
			int iItems = m_nickList.SendMsg(LB_GETCOUNT, 0, 0);
			for (int i = 0; i < iItems; i++) {
				USERINFO *ui = g_chatApi.UM_FindUserFromIndex(m_si, i);
				if (ui) {
					if (!wcsnicmp(ui->pszNick, m_wszSearch, mir_wstrlen(m_wszSearch))) {
						m_nickList.SendMsg(LB_SETCURSEL, i, 0);
						InvalidateRect(m_nickList.GetHwnd(), nullptr, FALSE);
						return 0;
					}
				}
			}

			MessageBeep(MB_OK);
			m_wszSearch[mir_wstrlen(m_wszSearch) - 1] = '\0';
			return 0;
		}
		break;
	}

	return CSuper::WndProc_Nicklist(msg, wParam, lParam);
}

/////////////////////////////////////////////////////////////////////////////////////////

void ShowRoom(SESSION_INFO *si)
{
	if (si == nullptr)
		return;

	// Do we need to create a window?
	CMsgDialog *pDlg;
	if (si->pDlg == nullptr) {
		pDlg = new CMsgDialog(si);
		pDlg->Show();
		
		si->pDlg = pDlg;
	}
	else pDlg = si->pDlg;
	
	SendMessage(pDlg->GetHwnd(), DM_UPDATETABCONTROL, -1, (LPARAM)si);
	SendMessage(GetParent(pDlg->GetHwnd()), CM_ACTIVATECHILD, 0, (LPARAM)pDlg->GetHwnd());
	SendMessage(GetParent(pDlg->GetHwnd()), CM_POPUPWINDOW, 0, (LPARAM)pDlg->GetHwnd());
	SendMessage(pDlg->GetHwnd(), WM_MOUSEACTIVATE, 0, 0);
	SetFocus(GetDlgItem(pDlg->GetHwnd(), IDC_SRMM_MESSAGE));
}
