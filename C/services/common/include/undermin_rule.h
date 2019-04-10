#ifndef _UNDERMIN_RULE_H
#define _UNDERMIN_RULE_H
/*
 * FogLAMP UnderMin builtin notification rule.
 *
 * Copyright (c) 2019 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Mark Riddoch
 */
#include <plugin.h>
#include <plugin_manager.h>
#include <config_category.h>
#include <rule_plugin.h>
#include <builtin_rule.h>

/**
 * UnderMinRule, derived from RulePlugin, is a builtin rule object
 */
class UnderMinRule : public RulePlugin
{
	public:
		UnderMinRule(const std::string& name);
	        ~UnderMinRule();

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
