#include "StdAfx.h"
#include "ssIfFolder.h"
#include "ssFolder.h"

ssFolder::ssFolder()
{
	m_parent = NULL;

	m_name = string("");
	m_home = 0;
	m_selected = 0;
	m_children.clear();
}

ssFolder::~ssFolder()
{
}

// --------------------------------
// ssIfFolder

ssIfFolder::Type ssFolder::GetType(void) const
{
	return ssIfFolder::TYPE_FOLDER;
}

ssIfFolder *ssFolder::GetParent(void) const
{
	return m_parent;
}

void ssFolder::SetParent(ssIfFolder *_parent)
{
	m_parent = _parent;
}

int ssFolder::GetChildCount(void) const
{
	return m_children.size();
}

ssIfFolder *ssFolder::GetChild(int _index) const
{
	return m_children[_index];
}

string ssFolder::GetName(void) const
{
	return m_name;
}

// --------------------------------
// ssFolder

void ssFolder::AddFolder(ssIfFolder *_folder)
{
	m_children.push_back(_folder);
}

void ssFolder::SetName(const string &_name)
{
	m_name = _name;
}

int ssFolder::GetHome(void) const
{
	return m_home;
}

void ssFolder::SetHome(int _home)
{
	m_home = _home;
}

int ssFolder::GetSelected(void) const
{
	return m_selected;
}

void ssFolder::SetSelected(int _selected)
{
	m_selected = _selected;
}
