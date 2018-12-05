#ifndef _OVERMAX_RULE_H
#define _OVERMAX_RULE_H
/*
 * FogLAMP OverMax builtin notification rule.
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */
#include <plugin.h>
#include <plugin_manager.h>
#include <config_category.h>
#include <rule_plugin.h>

class OverMaxRule : public RulePlugin
{
	public:
		OverMaxRule(const std::string& name);
	        ~OverMaxRule();

		PLUGIN_HANDLE		init(const ConfigCategory& config);
		void			shutdown();
		bool			persistData() { return info->options & SP_PERSIST_DATA; };
		std::string		triggers() const;
		bool			eval(const std::string& assetValues);
		std::string		reason() const;
		PLUGIN_INFORMATION*	getInfo();
		bool			isBuiltin() const { return true; };
};

#endif
