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
#include <builtin_rule.h>

/**
 * OverMaxRule, derived from RulePlugin, is a builtin rule object
 */
class OverMaxRule : public RulePlugin
{
	public:
		OverMaxRule(const std::string& name);
	        ~OverMaxRule();

		PLUGIN_HANDLE		init(const ConfigCategory& config);
		void			shutdown();
		bool			persistData() { return info->options & SP_PERSIST_DATA; };
		std::string		triggers();
		bool			eval(const std::string& assetValues);
		std::string		reason() const;
		PLUGIN_INFORMATION*	getInfo();
		bool			isBuiltin() const { return true; };
		void			configure(const ConfigCategory& config);
		void			reconfigure(const std::string& newConfig);
		bool			evalAsset(const Value& assetValue,
						  RuleTrigger* rule);
		bool			checkLimit(const std::string& name,
						   const Value& point,
						   double limitValue);
		bool			evalDatapoint(const std::string& name,
						      const Value& point,
						      double limitValue);
};

#endif
