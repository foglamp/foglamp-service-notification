#ifndef _THRESHOLD_RULE_H
#define _THRESHOLD_RULE_H
/*
 * Fledge Threshold builtin notification rule.
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

typedef enum {
	THRESHOLD_GREATER,
	THRESHOLD_GREATER_EQUAL,
	THRESHOLD_LESS,
	THRESHOLD_LESS_EQUAL
} ThresholdCondition;

/**
 * ThresholdRule, derived from RulePlugin, is a builtin rule object
 */
class ThresholdRule : public RulePlugin
{
	public:
		ThresholdRule(const std::string& name);
	        ~ThresholdRule();

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
		bool			checkLimit(const Value& point,
						   double limitValue);
	private:
		ThresholdCondition	m_condition;
};

#endif
