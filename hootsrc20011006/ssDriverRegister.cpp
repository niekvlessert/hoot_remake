#include "StdAfx.h"
#include "ssDriverConfig.h"
#include "ssDriverDescription.h"
#include "ssDriverBinder.h"

#include "ssDriverRegister.h"

// ドライバをシステムに登録するためのクラス
// static ssDriverRegister 適当な名前(string("ドライバ名"), Factory関数, ドライバ情報);


ssDriverRegister::ssDriverRegister(string &_major,
								   ssDriverFactoryFunction _func,
								   const ssDriverDescription *_desc)
{
	ssDriverBinder *binder = ssDriverBinder::Instance();

	binder->Register(_major, _func);

	if (_desc != NULL) {
		for (const ssDriverDescription *desc = _desc; desc->name != NULL; desc++) {
			binder->RegisterDescription(desc);
		}
	}
}
