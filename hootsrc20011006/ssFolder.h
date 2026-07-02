#ifndef __SSFOLDER_H__
#define __SSFOLDER_H__

#include "ssIfFolder.h"

class ssFolder : public ssIfFolder
{
public:
	ssFolder();
	~ssFolder();

public:
	// --------------------------------
	// ssIfFolder

	ssIfFolder::Type GetType(void) const;

	ssIfFolder *GetParent(void) const;
	void SetParent(ssIfFolder *_parent);

	int GetChildCount(void) const;
	ssIfFolder *GetChild(int _index) const;

	string GetName(void) const;

public:
	void AddFolder(ssIfFolder *_folder);

	void SetName(const string &_name);

	int GetHome(void) const;
	void SetHome(int _home);
	int GetSelected(void) const;
	void SetSelected(int _selected);

private:
	ssIfFolder *m_parent;
	string m_name;

	int m_home;
	int m_selected;

	vector<ssIfFolder *> m_children;
};

#endif // __SSFOLDER_H__
