#ifndef __SSIFFOLDER_H__
#define __SSIFFOLDER_H__

class ssIfFolder
{
public:
	enum Type
	{
		TYPE_FOLDER = 0,
		TYPE_NODE,
	};
public:
	virtual ~ssIfFolder() {}

public:
	virtual ssIfFolder::Type GetType(void) const = 0;

	virtual ssIfFolder *GetParent(void) const = 0;
	virtual void SetParent(ssIfFolder *_parent) = 0;

	virtual int GetChildCount(void) const = 0;
	virtual ssIfFolder *GetChild(int _index) const = 0;

	virtual string GetName(void) const = 0;
};

#endif // __SSIFELEMENT_H__
