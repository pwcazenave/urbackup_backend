/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "clientdao.h"
#include "../stringtools.h"
#include "../Interface/Server.h"
#include <memory.h>

ClientDAO::ClientDAO(IDatabase *pDB)
{
	db=pDB;
	prepareQueries();
}

void ClientDAO::prepareQueries(void)
{
	q_get_files=db->Prepare("SELECT data,num FROM files WHERE name=?", false);
	q_add_files=db->Prepare("INSERT INTO files_tmp (name, num, data) VALUES (?,?,?)", false);
	q_get_dirs=db->Prepare("SELECT name, path, id FROM backupdirs", false);
	q_remove_all=db->Prepare("DELETE FROM files", false);
	q_get_changed_dirs=db->Prepare("SELECT name FROM mdirs", false);
	q_remove_changed_dirs=db->Prepare("DELETE FROM mdirs", false);
	q_modify_files=db->Prepare("UPDATE files SET data=?, num=? WHERE name=?", false);
	q_has_files=db->Prepare("SELECT count(*) AS num FROM files WHERE name=?", false);
	q_insert_shadowcopy=db->Prepare("INSERT INTO shadowcopies (vssid, ssetid, target, path, tname, orig_target, filesrv) VALUES (?, ?, ?, ?, ?, ?, ?)", false);
	q_get_shadowcopies=db->Prepare("SELECT id, vssid, ssetid, target, path, tname, orig_target, filesrv FROM shadowcopies", false);
	q_remove_shadowcopies=db->Prepare("DELETE FROM shadowcopies WHERE id=?", false);
	q_save_changed_dirs=db->Prepare("INSERT INTO mdirs_backup SELECT name FROM mdirs", false);
	q_delete_saved_changed_dirs=db->Prepare("DELETE FROM mdirs_backup", false);
	q_restore_saved_changed_dirs=db->Prepare("INSERT INTO mdirs SELECT name FROM mdirs_backup", false);
	q_copy_from_tmp_files=db->Prepare("INSERT INTO files (num, data, name) SELECT num, data, name FROM files_tmp", false);
	q_delete_tmp_files=db->Prepare("DELETE FROM files_tmp", false);
	q_has_changed_gap=db->Prepare("SELECT name FROM mdirs WHERE name GLOB '##-GAP-##*'", false);
}

void ClientDAO::destroyQueries(void)
{
	db->destroyQuery(q_get_files);
	db->destroyQuery(q_add_files);
	db->destroyQuery(q_get_dirs);
	db->destroyQuery(q_remove_all);
	db->destroyQuery(q_get_changed_dirs);
	db->destroyQuery(q_remove_changed_dirs);
	db->destroyQuery(q_modify_files);
	db->destroyQuery(q_has_files);
	db->destroyQuery(q_insert_shadowcopy);
	db->destroyQuery(q_get_shadowcopies);
	db->destroyQuery(q_remove_shadowcopies);
	db->destroyQuery(q_save_changed_dirs);
	db->destroyQuery(q_delete_saved_changed_dirs);
	db->destroyQuery(q_restore_saved_changed_dirs);
	db->destroyQuery(q_copy_from_tmp_files);
	db->destroyQuery(q_delete_tmp_files);
	db->destroyQuery(q_has_changed_gap);
}

void ClientDAO::restartQueries(void)
{
	destroyQueries();
	prepareQueries();
}

bool ClientDAO::getFiles(std::wstring path, std::vector<SFile> &data)
{
	q_get_files->Bind(path);
	db_results res=q_get_files->Read();
	q_get_files->Reset();
	if(res.size()==0)
		return false;

	std::wstring &qdata=res[0][L"data"];

	if(qdata.empty())
		return true;

	int num=watoi(res[0][L"num"]);
	char *ptr=(char*)&qdata[0];
	while(ptr-(char*)&qdata[0]<num)
	{
		SFile f;
		unsigned short ss;
		memcpy(&ss, ptr, sizeof(unsigned short));
		ptr+=sizeof(unsigned short);
		std::string tmp;
		tmp.resize(ss);
		memcpy(&tmp[0], ptr, ss);
		f.name=Server->ConvertToUnicode(tmp);
		ptr+=ss;
		memcpy(&f.size, ptr, sizeof(int64));
		ptr+=sizeof(int64);
		memcpy(&f.last_modified, ptr, sizeof(int64));
		ptr+=sizeof(int64);
		char isdir=*ptr;
		++ptr;
		if(isdir==0)
			f.isdir=false;
		else
			f.isdir=true;
		data.push_back(f);
	}
	return true;
}

char * constructData(const std::vector<SFile> &data, size_t &datasize)
{
	datasize=0;
	std::vector<std::string> utf;
	for(size_t i=0;i<data.size();++i)
	{
		std::string us=Server->ConvertToUTF8(data[i].name);
		datasize+=us.size();
		datasize+=sizeof(unsigned short);
		datasize+=sizeof(int64)*2;
		++datasize;
		utf.push_back(us);
	}
	char *buffer=new char[datasize];
	char *ptr=buffer;
	for(size_t i=0;i<data.size();++i)
	{
		unsigned short ss=(unsigned short)utf[i].size();
		memcpy(ptr, (char*)&ss, sizeof(unsigned short));
		ptr+=sizeof(unsigned short);
		memcpy(ptr, &utf[i][0], ss);
		ptr+=ss;
		memcpy(ptr, (char*)&data[i].size, sizeof(int64));
		ptr+=sizeof(int64);
		memcpy(ptr, (char*)&data[i].last_modified, sizeof(int64));
		ptr+=sizeof(int64);
		char isdir=1;
		if(!data[i].isdir)
			isdir=0;
		*ptr=isdir;
		ptr+=sizeof(char);
	}
	return buffer;
}

void ClientDAO::addFiles(std::wstring path, const std::vector<SFile> &data)
{
	size_t ds;
	char *buffer=constructData(data, ds);
	q_add_files->Bind(path);
	q_add_files->Bind(ds);
	q_add_files->Bind(buffer, (_u32)ds);
	q_add_files->Write();
	q_add_files->Reset();
	delete []buffer;
}

void ClientDAO::modifyFiles(std::wstring path, const std::vector<SFile> &data)
{
	size_t ds;
	char *buffer=constructData(data, ds);
	q_modify_files->Bind(buffer, (_u32)ds);
	q_modify_files->Bind(ds);
	q_modify_files->Bind(path);
	q_modify_files->Write();
	q_modify_files->Reset();
	delete []buffer;
}

bool ClientDAO::hasFiles(std::wstring path)
{
	q_has_files->Bind(path);
	db_results res=q_has_files->Read();
	q_has_files->Reset();
	if(res.size()>0)
		return res[0][L"num"]==L"1";
	else
		return false;
}

std::vector<SBackupDir> ClientDAO::getBackupDirs(void)
{
	db_results res=q_get_dirs->Read();
	q_get_dirs->Reset();

	std::vector<SBackupDir> ret;
	for(size_t i=0;i<res.size();++i)
	{
		SBackupDir dir;
		dir.id=watoi(res[i][L"id"]);
		dir.tname=wnarrow(res[i][L"name"]);
		dir.path=res[i][L"path"];

		ret.push_back(dir);
	}
	return ret;
}

void ClientDAO::removeAllFiles(void)
{
	q_remove_all->Write();
}

std::vector<std::wstring> ClientDAO::getChangedDirs(bool del)
{
	std::vector<std::wstring> ret;
	db->BeginTransaction();
	db_results res=q_get_changed_dirs->Read();
	q_get_changed_dirs->Reset();
	if(del)
	{
		q_save_changed_dirs->Write();
		q_save_changed_dirs->Reset();
		q_remove_changed_dirs->Write();
		q_remove_changed_dirs->Reset();
	}
	db->EndTransaction();
	for(size_t i=0;i<res.size();++i)
	{
		ret.push_back(res[i][L"name"]);
	}
	return ret;
}

std::vector<SShadowCopy> ClientDAO::getShadowcopies(void)
{
	db_results res=q_get_shadowcopies->Read();
	q_get_shadowcopies->Reset();
	std::vector<SShadowCopy> ret;
	for(size_t i=0;i<res.size();++i)
	{
		db_single_result &r=res[i];
		SShadowCopy sc;
		sc.id=watoi(r[L"id"]);
		memcpy(&sc.vssid, r[L"vssid"].c_str(), sizeof(GUID) );
		memcpy(&sc.ssetid, r[L"ssetid"].c_str(), sizeof(GUID) );
		sc.target=r[L"target"];
		sc.path=r[L"path"];
		sc.tname=r[L"tname"];
		sc.orig_target=r[L"orig_target"];
		sc.filesrv=r[L"filesrv"]==L"0"?false:true;
		ret.push_back(sc);
	}
	return ret;
}

int ClientDAO::addShadowcopy(const SShadowCopy &sc)
{
	q_insert_shadowcopy->Bind((char*)&sc.vssid, sizeof(GUID) );
	q_insert_shadowcopy->Bind((char*)&sc.ssetid, sizeof(GUID) );
	q_insert_shadowcopy->Bind(sc.target);
	q_insert_shadowcopy->Bind(sc.path);
	q_insert_shadowcopy->Bind(sc.tname);
	q_insert_shadowcopy->Bind(sc.orig_target);
	q_insert_shadowcopy->Bind(sc.filesrv?1:0);
	q_insert_shadowcopy->Write();
	q_insert_shadowcopy->Reset();
	return (int)db->getLastInsertID();
}

void ClientDAO::deleteShadowcopy(int id)
{
	q_remove_shadowcopies->Bind(id);
	q_remove_shadowcopies->Write();
	q_remove_shadowcopies->Reset();
}

void ClientDAO::deleteSavedChangedDirs(void)
{
	q_delete_saved_changed_dirs->Write();
	q_delete_saved_changed_dirs->Reset();
}

void ClientDAO::restoreSavedChangedDirs(void)
{
	q_restore_saved_changed_dirs->Write();
	q_restore_saved_changed_dirs->Reset();
}

void ClientDAO::copyFromTmpFiles(void)
{
	q_copy_from_tmp_files->Write();
	q_copy_from_tmp_files->Reset();
	q_delete_tmp_files->Write();
	q_delete_tmp_files->Reset();
}

bool ClientDAO::hasChangedGap(void)
{
	db_results res=q_has_changed_gap->Read();
	q_has_changed_gap->Reset();
	return !res.empty();
}

void ClientDAO::deleteChangedDirs(void)
{
	q_remove_changed_dirs->Write();
	q_remove_changed_dirs->Reset();
}

std::vector<std::wstring> ClientDAO::getGapDirs(void)
{
	db_results res=q_has_changed_gap->Read();
	q_has_changed_gap->Reset();
	std::vector<std::wstring> ret;
	for(size_t i=0;i<res.size();++i)
	{
		std::wstring gap=res[i][L"name"];
		gap.erase(0,9);
		ret.push_back(gap);
	}
	return ret;
}