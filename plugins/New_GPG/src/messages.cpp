// Copyright © 2010-20 sss
// 
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#include "stdafx.h"

int returnNoError(MCONTACT hContact);

std::list<HANDLE> sent_msgs;

struct RecvParams
{
	RecvParams(MCONTACT _p1, std::wstring _p2, char *_p3, DWORD _p4) :
		hContact(_p1),
		str(_p2),
		msg(_p3),
		timestamp(_p4)
	{}

	MCONTACT hContact;
	std::wstring str;
	char *msg;
	DWORD timestamp;
};

static void RecvMsgSvc_func(RecvParams *param)
{
	DWORD dbflags = DBEF_UTF;
	MCONTACT hContact = param->hContact;
	{
		// check for gpg related data
		wstring::size_type s1 = param->str.find(L"-----BEGIN PGP MESSAGE-----");
		wstring::size_type s2 = param->str.find(L"-----END PGP MESSAGE-----");
		if (s2 != wstring::npos && s1 != wstring::npos) { //this is generic encrypted data block
			if (!isContactSecured(hContact)) {
				if (globals.bDebugLog)
					globals.debuglog << std::string(time_str() + ": info: received encrypted message from: " + toUTF8(Clist_GetContactDisplayName(hContact)) + " with turned off encryption");
				if (MessageBox(nullptr, TranslateT("We received encrypted message from contact with encryption turned off.\nDo you want to turn on encryption for this contact?"), TranslateT("Warning"), MB_YESNO) == IDYES) {
					if (!isContactHaveKey(hContact))
						ShowLoadPublicKeyDialog(hContact, true);
					else {
						g_plugin.setByte(db_mc_isMeta(hContact) ? metaGetMostOnline(hContact) : hContact, "GPGEncryption", 1);
						setSrmmIcon(hContact);
						setClistIcon(hContact);
					}

					if (isContactHaveKey(hContact)) {
						g_plugin.setByte(db_mc_isMeta(hContact) ? metaGetMostOnline(hContact) : hContact, "GPGEncryption", 1);
						setSrmmIcon(hContact);
						setClistIcon(hContact);
					}
				}
				else if (MessageBox(nullptr, TranslateT("Do you want to try to decrypt encrypted message?"), TranslateT("Warning"), MB_YESNO) == IDNO) {
					HistoryLog(hContact, db_event(param->msg, param->timestamp, 0, dbflags));
					delete param;
					return;
				}
			}
			else if (globals.bDebugLog)
				globals.debuglog << std::string(time_str() + ": info: received encrypted message from: " + toUTF8(Clist_GetContactDisplayName(hContact)));
			boost::algorithm::erase_all(param->str, "\r");
			s2 += mir_wstrlen(L"-----END PGP MESSAGE-----");

			ptrW ptszHomePath(g_plugin.getWStringA("szHomePath", L""));
			wstring encfile = toUTF16(get_random(10));
			wstring decfile = toUTF16(get_random(10));
			{
				wstring path = wstring(ptszHomePath) + L"\\tmp\\" + encfile;
				if (!globals.bDebugLog) {
					boost::system::error_code e;
					boost::filesystem::remove(path, e);
				}

				{
					const int timeout = 5000, step = 100;
					int count = 0;

					fstream f(path.c_str(), std::ios::out);
					while (!f.is_open()) {
						::Sleep(step);
						count += step;
						if (count >= timeout) {
							g_plugin.setByte(hContact, "GPGEncryption", 0);
							setSrmmIcon(hContact);
							setClistIcon(hContact);
							globals.debuglog << std::string(time_str() + "info: failed to create temporary file for decryption, disabling gpg for contact to avoid deadlock");
							delete param;
							return;
						}
						f.open(path.c_str(), std::ios::out);
					}
					char *tmp = mir_u2a(param->str.substr(s1, s2 - s1).c_str());
					f << tmp;
					mir_free(tmp);
					f.close();
				}

				gpg_execution_params params;
				params.addParam(L"--batch");
				{
					CMStringA inkeyid = g_plugin.getMStringA(db_mc_isMeta(hContact) ? metaGetMostOnline(hContact) : hContact, "InKeyID");
					CMStringW pass;
					if (!inkeyid.IsEmpty()) {
						string dbsetting = "szKey_";
						dbsetting += inkeyid;
						dbsetting += "_Password";
						pass = g_plugin.getMStringW(dbsetting.c_str());
						if (!pass.IsEmpty() && globals.bDebugLog)
							globals.debuglog << std::string(time_str() + ": info: found password in database for key ID: " + inkeyid.c_str() + ", trying to decrypt message from " + toUTF8(Clist_GetContactDisplayName(hContact)) + " with password");
					}
					else {
						pass = g_plugin.getMStringW("szKeyPassword");
						if (!pass.IsEmpty() && globals.bDebugLog)
							globals.debuglog << std::string(time_str() + ": info: found password for all keys in database, trying to decrypt message from " + toUTF8(Clist_GetContactDisplayName(hContact)) + " with password");
					}
					if (!pass.IsEmpty()) {
						params.addParam(L"--passphrase");
						params.addParam(pass.c_str());
					}
					else if (!globals.wszPassword.IsEmpty()) {
						if (globals.bDebugLog)
							globals.debuglog << std::string(time_str() + ": info: found password in memory, trying to decrypt message from " + toUTF8(Clist_GetContactDisplayName(hContact)) + " with password");
						params.addParam(L"--passphrase");
						params.addParam(globals.wszPassword.c_str());
					}
					else if (globals.bDebugLog)
						globals.debuglog << std::string(time_str() + ": info: passwords not found in database or memory, trying to decrypt message from " + toUTF8(Clist_GetContactDisplayName(hContact)) + " with out password");
				}

				if (!globals.bDebugLog) {
					boost::system::error_code e;
					boost::filesystem::remove(wstring(ptszHomePath) + L"\\tmp\\" + decfile, e);
				}

				params.addParam(L"--output");
				params.addParam(std::wstring(ptszHomePath) + L"\\tmp\\" + decfile);
				params.addParam(L"-d");
				params.addParam(L"-a");
				params.addParam(path);
				
				if (!gpg_launcher(params)) {
					if (!globals.bDebugLog) {
						boost::system::error_code e;
						boost::filesystem::remove(path, e);
					}
					HistoryLog(hContact, db_event(param->msg, param->timestamp, 0, dbflags));
					BYTE enc = g_plugin.getByte(hContact, "GPGEncryption", 0);
					g_plugin.setByte(hContact, "GPGEncryption", 0);
					ProtoChainSend(hContact, PSS_MESSAGE, 0, (LPARAM)"Unable to decrypt PGP encrypted message");
					HistoryLog(hContact, db_event("Error message sent", 0, 0, DBEF_SENT));
					g_plugin.setByte(hContact, "GPGEncryption", enc);
					delete param;
					return;
				}
				if (params.result == pxNotFound) {
					if (!globals.bDebugLog) {
						boost::system::error_code e;
						boost::filesystem::remove(path, e);
					}

					HistoryLog(hContact, db_event(param->msg, param->timestamp, 0, dbflags));
					delete param;
					return;
				}

				// TODO: check gpg output for errors
				globals._terminate = false;

				string out(params.out);
				while (out.find("public key decryption failed: bad passphrase") != string::npos) {
					if (globals.bDebugLog)
						globals.debuglog << std::string(time_str() + ": info: failed to decrypt messaage from " + toUTF8(Clist_GetContactDisplayName(hContact)) + " password needed, trying to get one");
					if (globals._terminate) {
						BYTE enc = g_plugin.getByte(hContact, "GPGEncryption", 0);
						g_plugin.setByte(hContact, "GPGEncryption", 0);
						ProtoChainSend(hContact, PSS_MESSAGE, 0, (LPARAM)"Unable to decrypt PGP encrypted message");
						HistoryLog(hContact, db_event("Error message sent", 0, 0, DBEF_SENT));
						g_plugin.setByte(hContact, "GPGEncryption", enc);
						break;
					}
					{ //save inkey id
						string::size_type s = out.find(" encrypted with ");
						s = out.find(" ID ", s);
						s += mir_strlen(" ID ");
						g_plugin.setString(db_mc_isMeta(hContact) ? metaGetMostOnline(hContact) : hContact, "InKeyID", out.substr(s, out.find(",", s) - s).c_str());
					}

					CDlgKeyPasswordMsgBox *d = new CDlgKeyPasswordMsgBox(hContact);
					d->DoModal();

					gpg_execution_params params2;
					params2.aargv = params.aargv;
					if (!globals.wszPassword.IsEmpty()) {
						if (globals.bDebugLog)
							globals.debuglog << std::string(time_str() + ": info: found password in memory, trying to decrypt message from " + toUTF8(Clist_GetContactDisplayName(hContact)));

						params2.addParam(L"--passphrase");
						params2.addParam(globals.wszPassword.c_str());
					}

					if (!gpg_launcher(params2)) {
						if (!globals.bDebugLog) {
							boost::system::error_code e;
							boost::filesystem::remove(path, e);
						}

						HistoryLog(hContact, db_event(param->msg, param->timestamp, 0, dbflags));
						BYTE enc = g_plugin.getByte(hContact, "GPGEncryption", 0);
						g_plugin.setByte(hContact, "GPGEncryption", 0);
						ProtoChainSend(hContact, PSS_MESSAGE, 0, (LPARAM)"Unable to decrypt PGP encrypted message");
						HistoryLog(hContact, db_event("Error message sent", 0, 0, DBEF_SENT));
						g_plugin.setByte(hContact, "GPGEncryption", enc);
						delete param;
						return;
					}
					if (params2.result == pxNotFound) {
						if (!globals.bDebugLog) {
							boost::system::error_code e;
							boost::filesystem::remove(path, e);
						}

						HistoryLog(hContact, db_event(param->msg, param->timestamp, 0, dbflags));
						delete param;
						return;
					}
				}
				out.clear();
				if (!gpg_launcher(params)) {
					if (!globals.bDebugLog) {
						boost::system::error_code e;
						boost::filesystem::remove(path, e);
					}

					HistoryLog(hContact, db_event(param->msg, param->timestamp, 0, dbflags));
					BYTE enc = g_plugin.getByte(hContact, "GPGEncryption", 0);
					g_plugin.setByte(hContact, "GPGEncryption", 0);
					ProtoChainSend(hContact, PSS_MESSAGE, 0, (LPARAM)"Unable to decrypt PGP encrypted message");
					HistoryLog(hContact, db_event("Error message sent", 0, 0, DBEF_SENT));
					g_plugin.setByte(hContact, "GPGEncryption", enc);
					delete param;
					return;
				}

				if (params.result == pxNotFound) {
					if (!globals.bDebugLog) {
						boost::system::error_code e;
						boost::filesystem::remove(path, e);
					}

					HistoryLog(hContact, db_event(param->msg, param->timestamp, 0, dbflags));
				}

				if (!globals.bDebugLog) {
					boost::system::error_code e;
					boost::filesystem::remove(wstring(ptszHomePath) + L"\\tmp\\" + encfile, e);
				}

				if (!boost::filesystem::exists(wstring(ptszHomePath) + L"\\tmp\\" + decfile)) {
					string str1 = param->msg;
					str1.insert(0, "Received unencrypted message:\n");
					if (globals.bDebugLog)
						globals.debuglog << std::string(time_str() + ": info: Failed to decrypt GPG encrypted message.");

					ptrA tmp4((char*)mir_alloc(sizeof(char)*(str1.length() + 1)));
					mir_strcpy(tmp4, str1.c_str());
					HistoryLog(hContact, db_event(param->msg, param->timestamp, 0, dbflags));
					BYTE enc = g_plugin.getByte(hContact, "GPGEncryption", 0);
					g_plugin.setByte(hContact, "GPGEncryption", 0);
					ProtoChainSend(hContact, PSS_MESSAGE, 0, (LPARAM)"Unable to decrypt PGP encrypted message");
					HistoryLog(hContact, db_event("Error message sent", 0, 0, DBEF_SENT));
					g_plugin.setByte(hContact, "GPGEncryption", enc);
					delete param;
					return;
				}

				param->str.clear();

				wstring tszDecPath = wstring(ptszHomePath) + L"\\tmp\\" + decfile;
				{
					fstream f(tszDecPath.c_str(), std::ios::in | std::ios::ate | std::ios::binary);
					if (f.is_open()) {
						size_t size = f.tellg();
						char *tmp = new char[size + 1];
						f.seekg(0, std::ios::beg);
						f.read(tmp, size);
						tmp[size] = '\0';
						toUTF16(tmp);
						param->str.append(toUTF16(tmp));
						delete[] tmp;
						f.close();
						if (!globals.bDebugLog) {
							boost::system::error_code ec;
							boost::filesystem::remove(tszDecPath, ec);
							if (ec) {
								//TODO: handle error
							}
						}
					}
				}
				if (param->str.empty()) {
					string szMsg = param->msg;
					szMsg.insert(0, "Failed to decrypt GPG encrypted message.\nMessage body for manual decryption:\n");
					if (globals.bDebugLog)
						globals.debuglog << std::string(time_str() + ": info: Failed to decrypt GPG encrypted message.");

					HistoryLog(hContact, db_event(param->msg, param->timestamp, 0, dbflags));
					BYTE enc = g_plugin.getByte(hContact, "GPGEncryption", 0);
					g_plugin.setByte(hContact, "GPGEncryption", 0);
					ProtoChainSend(hContact, PSS_MESSAGE, 0, (LPARAM)"Unable to decrypt PGP encrypted message");
					HistoryLog(hContact, db_event("Error message sent", 0, 0, DBEF_SENT));
					g_plugin.setByte(hContact, "GPGEncryption", enc);
					delete param;
					return;
				}

				fix_line_term(param->str);
				if (globals.bAppendTags) {
					param->str.insert(0, globals.wszInopentag);
					param->str.append(globals.wszInclosetag);
				}

				char *tmp = mir_strdup(toUTF8(param->str).c_str());
				HistoryLog(hContact, db_event(tmp, param->timestamp, 0, dbflags));
				mir_free(tmp);
				delete param;
				return;
			}
		}
	}
	if (g_plugin.getByte(db_mc_isMeta(hContact) ? metaGetMostOnline(hContact) : hContact, "GPGEncryption")) {
		HistoryLog(hContact, db_event(param->msg, param->timestamp, 0, dbflags | DBEF_READ));
		delete param;
		return;
	}

	HistoryLog(hContact, db_event(param->msg, param->timestamp, 0, dbflags));
	delete param;
}

INT_PTR RecvMsgSvc(WPARAM w, LPARAM l)
{
	CCSDATA *ccs = (CCSDATA*)l;
	if (!ccs)
		return Proto_ChainRecv(w, ccs);
	PROTORECVEVENT *pre = (PROTORECVEVENT*)(ccs->lParam);
	if (!pre)
		return Proto_ChainRecv(w, ccs);
	char *msg = pre->szMessage;
	if (!msg)
		return Proto_ChainRecv(w, ccs);
	DWORD dbflags = DBEF_UTF;
	if (db_mc_isMeta(ccs->hContact)) {
		if (!strstr(msg, "-----BEGIN PGP MESSAGE-----"))
			return Proto_ChainRecv(w, ccs);
		else {
			if (globals.bDebugLog)
				globals.debuglog << std::string(time_str() + ": info: blocked pgp message to metacontact:" + toUTF8(Clist_GetContactDisplayName(ccs->hContact)));
			return 0;
		}
	}
	wstring str = toUTF16(msg);
	size_t s1, s2;
	if (globals.bAutoExchange && (str.find(L"-----PGP KEY RESPONSE-----") != wstring::npos)) {
		if (globals.bDebugLog)
			globals.debuglog << std::string(time_str() + ": info(autoexchange): parsing key response:" + toUTF8(Clist_GetContactDisplayName(ccs->hContact)));
		s2 = str.find(L"-----END PGP PUBLIC KEY BLOCK-----");
		s1 = str.find(L"-----BEGIN PGP PUBLIC KEY BLOCK-----");
		if (s1 != wstring::npos && s2 != wstring::npos) {
			if (globals.bDebugLog)
				globals.debuglog << std::string(time_str() + ": info(autoexchange): found pubkey block:" + toUTF8(Clist_GetContactDisplayName(ccs->hContact)));
			s2 += mir_wstrlen(L"-----END PGP PUBLIC KEY BLOCK-----");
			g_plugin.setWString(ccs->hContact, "GPGPubKey", str.substr(s1, s2 - s1).c_str());
			{
				// gpg execute block
				CMStringW tmp2(g_plugin.getMStringW("szHomePath"));
				tmp2 += L"\\";
				tmp2 += get_random(5).c_str();
				tmp2 += L".asc";

				if (!globals.bDebugLog) {
					boost::system::error_code e;
					boost::filesystem::remove(tmp2.c_str(), e);
				}
				wfstream f(tmp2, std::ios::out);
				{
					const int timeout = 5000, step = 100;
					int count = 0;
					while (!f.is_open()) {
						::Sleep(step);
						count += step;
						if (count >= timeout) {
							g_plugin.setByte(ccs->hContact, "GPGEncryption", 0);
							setSrmmIcon(ccs->hContact);
							setClistIcon(ccs->hContact);
							globals.debuglog << std::string(time_str() + "info: failed to create temporary file for decryption, disabling gpg for contact to avoid deadlock");
							return 1;
						}
						f.open(tmp2, std::ios::out);
					}
				}
				f << g_plugin.getMStringW(ccs->hContact, "GPGPubKey").c_str();
				f.close();

				gpg_execution_params params;
				params.addParam(L"--batch");
				params.addParam(L"--import");
				params.addParam(tmp2.c_str());
				if (!gpg_launcher(params))
					return 1;

				if (!globals.bDebugLog) {
					boost::system::error_code e;
					boost::filesystem::remove(tmp2.c_str(), e);
				}
				if (params.result == pxNotFound)
					return 1;

				string output(params.out);
				s1 = output.find("gpg: key ") + mir_strlen("gpg: key ");
				s2 = output.find(":", s1);
				g_plugin.setString(ccs->hContact, "KeyID", output.substr(s1, s2 - s1).c_str());
				s2 += 2;
				s1 = output.find(RUS_QUOTE, s2);
				if (s1 == string::npos) {
					s1 = output.find("\"", s2);
					s1 += 1;
				}
				else s1 += sizeof(RUS_QUOTE) - 1;

				if ((s2 = output.find("(", s1)) == string::npos)
					s2 = output.find("<", s1);
				else if (s2 > output.find("<", s1))
					s2 = output.find("<", s1);
					
				char *tmp = (char*)mir_alloc(output.substr(s1, s2 - s1 - 1).length() + 1);
				mir_strcpy(tmp, output.substr(s1, s2 - s1 - 1).c_str());
				mir_utf8decode(tmp, nullptr);
				g_plugin.setString(ccs->hContact, "KeyMainName", tmp);
				mir_free(tmp);
				if ((s1 = output.find(")", s2)) == string::npos)
					s1 = output.find(">", s2);
				else if (s1 > output.find(">", s2))
					s1 = output.find(">", s2);
				s2++;
				if (output[s1] == ')') {
					tmp = (char*)mir_alloc(output.substr(s2, s1 - s2).length() + 1);
					mir_strcpy(tmp, output.substr(s2, s1 - s2).c_str());
					mir_utf8decode(tmp, nullptr);
					g_plugin.setString(ccs->hContact, "KeyComment", tmp);
					mir_free(tmp);
					s1 += 3;
					s2 = output.find(">", s1);
					tmp = (char*)mir_alloc(output.substr(s1, s2 - s1).length() + 1);
					mir_strcpy(tmp, output.substr(s1, s2 - s1).c_str());
					mir_utf8decode(tmp, nullptr);
					g_plugin.setString(ccs->hContact, "KeyMainEmail", tmp);
					mir_free(tmp);
				}
				else {
					tmp = (char*)mir_alloc(output.substr(s2, s1 - s2).length() + 1);
					mir_strcpy(tmp, output.substr(s2, s1 - s2).c_str());
					mir_utf8decode(tmp, nullptr);
					g_plugin.setString(ccs->hContact, "KeyMainEmail", output.substr(s2, s1 - s2).c_str());
					mir_free(tmp);
				}
				g_plugin.setByte(ccs->hContact, "GPGEncryption", 1);
				g_plugin.setByte(ccs->hContact, "bAlwatsTrust", 1);
				setSrmmIcon(ccs->hContact);
				setClistIcon(ccs->hContact);
				if (db_mc_isSub(ccs->hContact)) {
					setSrmmIcon(db_mc_getMeta(ccs->hContact));
					setClistIcon(db_mc_getMeta(ccs->hContact));
				}
				HistoryLog(ccs->hContact, "PGP Encryption turned on by key autoexchange feature");
			}
			return 1;
		}
	}
	if (((s2 = str.find(L"-----END PGP PUBLIC KEY BLOCK-----")) == wstring::npos) || ((s1 = str.find(L"-----BEGIN PGP PUBLIC KEY BLOCK-----")) == wstring::npos)) {
		s2 = str.find(L"-----END PGP PRIVATE KEY BLOCK-----");
		s1 = str.find(L"-----BEGIN PGP PRIVATE KEY BLOCK-----");
	}
	if ((s2 != wstring::npos) && (s1 != wstring::npos)) {  //this is public key
		if (globals.bDebugLog)
			globals.debuglog << std::string(time_str() + ": info: received key from: " + toUTF8(Clist_GetContactDisplayName(ccs->hContact)));
		s1 = 0;
		while ((s1 = str.find(L"\r", s1)) != wstring::npos)
			str.erase(s1, 1);
		if (((s2 = str.find(L"-----END PGP PUBLIC KEY BLOCK-----")) != wstring::npos) && ((s1 = str.find(L"-----BEGIN PGP PUBLIC KEY BLOCK-----")) != wstring::npos))
			s2 += mir_wstrlen(L"-----END PGP PUBLIC KEY BLOCK-----");
		else if (((s2 = str.find(L"-----BEGIN PGP PRIVATE KEY BLOCK-----")) != wstring::npos) && ((s1 = str.find(L"-----END PGP PRIVATE KEY BLOCK-----")) != wstring::npos))
			s2 += mir_wstrlen(L"-----END PGP PRIVATE KEY BLOCK-----");
		CDlgNewKey *d = new CDlgNewKey(ccs->hContact, str.substr(s1, s2 - s1));
		d->Show();
		HistoryLog(ccs->hContact, db_event(msg, 0, 0, dbflags));
		return 0;
	}
	if (globals.bAutoExchange && strstr(msg, "-----PGP KEY REQUEST-----") && globals.gpg_valid && globals.gpg_keyexist) {
		if (globals.bDebugLog)
			globals.debuglog << std::string(time_str() + ": info(autoexchange): received key request from: " + toUTF8(Clist_GetContactDisplayName(ccs->hContact)));

		CMStringA tmp(g_plugin.getMStringA("GPGPubKey"));
		if (!tmp.IsEmpty()) {
			int enc_state = g_plugin.getByte(ccs->hContact, "GPGEncryption");
			if (enc_state)
				g_plugin.setByte(ccs->hContact, "GPGEncryption", 0);

			string str1 = "-----PGP KEY RESPONSE-----";
			str1.append(tmp);
			ProtoChainSend(ccs->hContact, PSS_MESSAGE, 0, (LPARAM)str1.c_str());
			if (enc_state)
				g_plugin.setByte(ccs->hContact, "GPGEncryption", 1);
		}
		return 0;
	}
	else if (!isContactHaveKey(ccs->hContact) && globals.bAutoExchange && globals.gpg_valid && globals.gpg_keyexist) {
		char *proto = Proto_GetBaseAccountName(ccs->hContact);
		ptrA jid(db_get_utfa(ccs->hContact, proto, "jid", ""));
		if (jid[0]) {
			for (auto p : globals.Accounts) {
				ptrA caps(p->getJabberInterface()->GetResourceFeatures(jid));
				if (caps) {
					string str1;
					for (int i = 0;; i++) {
						str1.push_back(caps[i]);
						if (caps[i] == '\0')
							if (caps[i + 1] == '\0')
								break;
					}

					if (str1.find("GPG_Key_Auto_Exchange:0") != string::npos) {
						ProtoChainSend(ccs->hContact, PSS_MESSAGE, 0, (LPARAM)"-----PGP KEY REQUEST-----");
						return 0;
					}
				}
			}
		}
	}

	if (!strstr(msg, "-----BEGIN PGP MESSAGE-----"))
		return Proto_ChainRecv(w, ccs);

	mir_forkThread<RecvParams>(RecvMsgSvc_func, new RecvParams(ccs->hContact, str, msg, pre->timestamp));
	return 0;
}

void SendMsgSvc_func(MCONTACT hContact, char *msg, DWORD flags)
{
	wstring str = toUTF16(msg);
	if (globals.bStripTags && globals.bAppendTags) {
		if (globals.bDebugLog)
			globals.debuglog << std::string(time_str() + ": info: stripping tags in outgoing message, name: " + toUTF8(Clist_GetContactDisplayName(hContact)));
		strip_tags(str);
	}

	wstring file = toUTF16(get_random(10));
	gpg_execution_params params;
	{
		CMStringA tmp(g_plugin.getMStringA(hContact, "KeyID"));
		if (tmp.IsEmpty()) {
			HistoryLog(hContact, db_event("Failed to encrypt message with GPG (not found key for encryption in db)", 0, 0, DBEF_SENT));
			ProtoChainSend(hContact, PSS_MESSAGE, flags, (LPARAM)msg);
			return;
		}
		
		if (!globals.bJabberAPI)  { //force jabber to handle encrypted message by itself
			params.addParam(L"--comment");
			params.addParam(L"\"\"");
			params.addParam(L"--no-version");
		}
		if (g_plugin.getByte(hContact, "bAlwaysTrust", 0)) {
			params.addParam(L"--trust-model");
			params.addParam(L"always");
		}
		params.addParam(L"--batch");
		params.addParam(L"--yes");
		params.addParam(L"-eatr");
		wchar_t *tmp2 = mir_a2u(tmp);

		params.addParam(tmp2);
		mir_free(tmp2);
	}

	CMStringW path(g_plugin.getMStringW("szHomePath"));
	path += L"\\tmp\\";
	path += file.c_str();
	params.addParam(path.c_str());

	const int timeout = 5000, step = 100;
	int count = 0;
	{
		fstream f(path.c_str(), std::ios::out);
		while (!f.is_open()) {
			::Sleep(step);
			count += step;
			if (count >= timeout) {
				g_plugin.setByte(hContact, "GPGEncryption", 0); //disable encryption
				setSrmmIcon(hContact);
				setClistIcon(hContact);
				globals.debuglog << std::string(time_str() + ": info: failed to create temporary file for encryption, disabling encryption to avoid deadlock");
				break;
			}
			f.open(path.c_str(), std::ios::out);
		}
		if (count < timeout) {
			std::string tmp = toUTF8(str);
			f.write(tmp.c_str(), tmp.size());
			f.close();
		}
	}

	if (!gpg_launcher(params)) {
		ProtoChainSend(hContact, PSS_MESSAGE, flags, (LPARAM)msg);
		return;
	}

	if (params.result == pxNotFound) {
		ProtoChainSend(hContact, PSS_MESSAGE, flags, (LPARAM)msg);
		return;
	}

	if (params.out.Find("There is no assurance this key belongs to the named user") != -1) {
		if (IDYES != MessageBox(nullptr, TranslateT("We're trying to encrypt with untrusted key. Do you want to trust this key permanently?"), TranslateT("Warning"), MB_YESNO))
			return;

		g_plugin.setByte(hContact, "bAlwaysTrust", 1);

		params.addParam(L"--trust-model");
		params.addParam(L"always");
		if (!gpg_launcher(params)) {
			ProtoChainSend(hContact, PSS_MESSAGE, flags, (LPARAM)msg);
			return;
		}
		if (params.result == pxNotFound) {
			ProtoChainSend(hContact, PSS_MESSAGE, flags, (LPARAM)msg);
			return;
		}
		//TODO: check gpg output for errors
	}

	if (params.out.Find("usage: ") != -1) {
		MessageBox(nullptr, TranslateT("Something is wrong, GPG does not understand us, aborting encryption."), TranslateT("Warning"), MB_OK);
		//mir_free(msg);
		ProtoChainSend(hContact, PSS_MESSAGE, flags, (LPARAM)msg);
		if (!globals.bDebugLog) {
			boost::system::error_code e;
			boost::filesystem::remove(path.c_str(), e);
		}
		return;
	}

	if (!globals.bDebugLog) {
		boost::system::error_code e;
		boost::filesystem::remove(path.c_str(), e);
	}

	path += L".asc";
	wfstream f(path.c_str(), std::ios::in | std::ios::ate | std::ios::binary);
	count = 0;
	while (!f.is_open()) {
		::Sleep(step);
		f.open(path.c_str(), std::ios::in | std::ios::ate | std::ios::binary);
		count += step;
		if (count >= timeout) {
			g_plugin.setByte(hContact, "GPGEncryption", 0); //disable encryption
			setSrmmIcon(hContact);
			setClistIcon(hContact);
			globals.debuglog << std::string(time_str() + ": info: gpg failed to encrypt message, disabling encryption to avoid deadlock");
			break;
		}
	}

	str.clear();
	if (f.is_open()) {
		std::wifstream::pos_type size = f.tellg();
		wchar_t *tmp = new wchar_t[(std::ifstream::pos_type)size + (std::ifstream::pos_type)1];
		f.seekg(0, std::ios::beg);
		f.read(tmp, size);
		tmp[size] = '\0';
		str.append(tmp);
		delete[] tmp;
		f.close();
		if (!globals.bDebugLog) {
			boost::system::error_code e;
			boost::filesystem::remove(path.c_str(), e);
		}
	}

	if (str.empty()) {
		HistoryLog(hContact, db_event("Failed to encrypt message with GPG", 0, 0, DBEF_SENT));
		if (globals.bDebugLog)
			globals.debuglog << std::string(time_str() + ": info: Failed to encrypt message with GPG");
		ProtoChainSend(hContact, PSS_MESSAGE, flags, (LPARAM)msg);
		return;
	}

	string str_event = msg;
	if (globals.bAppendTags) {
		str_event.insert(0, toUTF8(globals.wszOutopentag.c_str()));
		str_event.append(toUTF8(globals.wszOutclosetag.c_str()));
	}

	if (globals.bDebugLog)
		globals.debuglog << std::string(time_str() + ": adding event to contact: " + toUTF8(Clist_GetContactDisplayName(hContact)) + " on send message.");

	fix_line_term(str);
	sent_msgs.push_back((HANDLE)ProtoChainSend(hContact, PSS_MESSAGE, flags, (LPARAM)toUTF8(str).c_str()));
}

INT_PTR SendMsgSvc(WPARAM w, LPARAM l)
{
	CCSDATA *ccs = (CCSDATA*)l;
	if (!ccs)
		return Proto_ChainSend(w, ccs);

	if (!ccs->lParam)
		return Proto_ChainSend(w, ccs);

	char *msg = (char*)ccs->lParam;
	if (!msg) {
		if (globals.bDebugLog)
			globals.debuglog << std::string(time_str() + ": info: failed to get message data, name: " + toUTF8(Clist_GetContactDisplayName(ccs->hContact)));
		return Proto_ChainSend(w, ccs);
	}

	if (strstr(msg, "-----BEGIN PGP MESSAGE-----")) {
		if (globals.bDebugLog)
			globals.debuglog << std::string(time_str() + ": info: encrypted message, let it go, name: " + toUTF8(Clist_GetContactDisplayName(ccs->hContact)));
		return Proto_ChainSend(w, ccs);
	}

	if (globals.bDebugLog)
		globals.debuglog << std::string(time_str() + ": info: contact have key, name: " + toUTF8(Clist_GetContactDisplayName(ccs->hContact)));

	if (globals.bDebugLog && db_mc_isMeta(ccs->hContact))
		globals.debuglog << std::string(time_str() + ": info: protocol is metacontacts, name: " + toUTF8(Clist_GetContactDisplayName(ccs->hContact)));

	if (!isContactSecured(ccs->hContact) || db_mc_isMeta(ccs->hContact)) {
		if (globals.bDebugLog)
			globals.debuglog << std::string(time_str() + ": info: contact not secured, name: " + toUTF8(Clist_GetContactDisplayName(ccs->hContact)));
		return Proto_ChainSend(w, ccs);
	}

	return returnNoError(ccs->hContact);
}

int HookSendMsg(WPARAM w, LPARAM l)
{
	if (!l)
		return 0;

	DBEVENTINFO * dbei = (DBEVENTINFO*)l;
	if (dbei->eventType != EVENTTYPE_MESSAGE)
		return 0;

	MCONTACT hContact = (MCONTACT)w;
	if (dbei->flags & DBEF_SENT) {
		if (isContactSecured(hContact) && strstr((char*)dbei->pBlob, "-----BEGIN PGP MESSAGE-----")) //our service data, can be double added by metacontacts e.w.c.
		{
			if (globals.bDebugLog)
				globals.debuglog << std::string(time_str() + ": info(send handler): block pgp message event, name: " + toUTF8(Clist_GetContactDisplayName(hContact)));
			return 1;
		}
		if (globals.bAutoExchange && (strstr((char*)dbei->pBlob, "-----PGP KEY RESPONSE-----") || strstr((char*)dbei->pBlob, "-----PGP KEY REQUEST-----"))) ///do not show service data in history
		{
			if (globals.bDebugLog)
				globals.debuglog << std::string(time_str() + ": info(send handler): block pgp key request/response event, name: " + toUTF8(Clist_GetContactDisplayName(hContact)));
			return 1;
		}
	}

	if (db_mc_isMeta(hContact))
		return 0;

	if (!isContactHaveKey(hContact)) {
		if (globals.bDebugLog)
			globals.debuglog << std::string(time_str() + ": info: contact have not key, name: " + toUTF8(Clist_GetContactDisplayName(hContact)));
		if (globals.bAutoExchange && !strstr((char*)dbei->pBlob, "-----PGP KEY REQUEST-----") && !strstr((char*)dbei->pBlob, "-----BEGIN PGP PUBLIC KEY BLOCK-----") && globals.gpg_valid) {
			if (globals.bDebugLog)
				globals.debuglog << std::string(time_str() + ": info: checking for autoexchange possibility, name: " + toUTF8(Clist_GetContactDisplayName(hContact)));

			LPSTR proto = Proto_GetBaseAccountName(hContact);
			ptrA jid(db_get_utfa(hContact, proto, "jid", ""));
			if (jid[0]) {
				if (globals.bDebugLog)
					globals.debuglog << std::string(time_str() + ": info(autoexchange): protocol looks like jabber, name: " + toUTF8(Clist_GetContactDisplayName(hContact)));
				for (auto p : globals.Accounts) {
					ptrA caps(p->getJabberInterface()->GetResourceFeatures(jid));
					if (caps) {
						string str;
						for (int i = 0;; i++) {
							str.push_back(caps[i]);
							if (caps[i] == '\0')
								if (caps[i + 1] == '\0')
									break;
						}

						if (str.find("GPG_Key_Auto_Exchange:0") != string::npos) {
							if (globals.bDebugLog)
								globals.debuglog << std::string(time_str() + ": info(autoexchange, jabber): autoexchange capability found, sending key request, name: " + toUTF8(Clist_GetContactDisplayName(hContact)));
							ProtoChainSend(hContact, PSS_MESSAGE, 0, (LPARAM)"-----PGP KEY REQUEST-----");
							globals.hcontact_data[hContact].msgs_to_send.push_back((char*)dbei->pBlob);
							mir_forkthread(send_encrypted_msgs_thread, (void*)hContact);
							return 0;
						}
					}
				}
			}
		}
		else return 0;
	}

	if (isContactSecured(hContact) && (dbei->flags & DBEF_SENT)) //aggressive outgoing events filtering
	{
		SendMsgSvc_func(hContact, (char*)dbei->pBlob, 0);
		//TODO: handle errors somehow ...
		if (globals.bAppendTags) {
			string str_event = (char*)dbei->pBlob;
			//mir_free(dbei->pBlob);
			str_event.insert(0, toUTF8(globals.wszOutopentag.c_str()));
			str_event.append(toUTF8(globals.wszOutclosetag.c_str()));
			dbei->pBlob = (PBYTE)mir_strdup(str_event.c_str());
			dbei->cbBlob = (DWORD)str_event.length() + 1;
		}

		return 0;
	}

	if (!isContactSecured(hContact)) {
		if (globals.bDebugLog)
			globals.debuglog << std::string(time_str() + ": event message: \"" + (char*)dbei->pBlob + "\" passed event filter, contact " + toUTF8(Clist_GetContactDisplayName(hContact)) + " is unsecured");
		return 0;
	}

	if (!(dbei->flags & DBEF_SENT) && db_mc_isMeta((MCONTACT)w)) {
		char tmp[29];
		strncpy(tmp, (char*)dbei->pBlob, 27);
		tmp[28] = '\0';
		if (strstr(tmp, "-----BEGIN PGP MESSAGE-----")) {
			if (globals.bDebugLog)
				globals.debuglog << std::string(time_str() + ": info(send handler): block pgp message event, name: " + toUTF8(Clist_GetContactDisplayName(hContact)));
			return 1;
		}
	}
	return 0;
}
