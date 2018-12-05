/**
 * FogLAMP OverMax builtin notification rule
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */

#include <overmax_rule.h>

#define RULE_NAME "OverMax"

/**
 * Rule specific default configuration
 */
#define RULE_DEFAULT_CONFIG \
			"\"description\": { " \
				"\"description\": \"" RULE_NAME " notification rule\", " \
				"\"type\": \"string\", " \
				"\"default\": \"The value of a reading data exceeds an absolute limit value\", " \
				"\"order\": \"1\" }"

#define BUITIN_RULE_DESC "\"plugin\": {\"description\": \"" RULE_NAME " notification rule\", " \
			"\"type\": \"string\", \"default\": \"" RULE_NAME "\", \"readonly\": \"true\"}"

#define RULE_DEFAULT_CONFIG_INFO "{" BUITIN_RULE_DESC ", " RULE_DEFAULT_CONFIG "}"

using namespace std;

/**
 * The C API rule information structure
 */
static PLUGIN_INFORMATION ruleInfo = {
	RULE_NAME,			// Name
	"1.0.0",			// Version
	0,				// Flags
	PLUGIN_TYPE_NOTIFICATION_RULE,	// Type
	"1.0.0",			// Interface version
	RULE_DEFAULT_CONFIG_INFO	// Configuration
};

/**
 * OverMaxRule builtin rule constructor
 *
 * Call parent class RulePlugin constructor
 * passing a NULL plugin handle 
 *
 * @param    name	The builtin rule name
 */
OverMaxRule::OverMaxRule(const std::string& name) :
			 RulePlugin(name, NULL)
{
}

/**
 * OverMaxRule builtin rule destructor
 */
OverMaxRule::~OverMaxRule()
{
}

/**
 * Return rule info
 */
PLUGIN_INFORMATION* OverMaxRule::getInfo()
{       
	return &ruleInfo;
}

/**
 * Initialise rule objects based in configuration
 *
 * @param    config	The rule configuration category data.
 * @return		The rule handle.
 */
PLUGIN_HANDLE OverMaxRule::init(const ConfigCategory& config)
{
	string* data = new string("OverMaxRule");

	m_instance = (PLUGIN_HANDLE)data;

	return (m_instance ? &m_instance : NULL);
}

/**
 * Free rule resources
 */
void OverMaxRule::shutdown()
{
	string* data = (string *)m_instance;
	// Delete plugin handle
	delete data;
}

/**
 * Return triggers JSON document
 *
 * @return	JSON string
 */
string OverMaxRule::triggers() const
{
	string ret = "{}";
 
	return ret;
}

/**
 * Evaluate notification data received
 *
 * @param    assetValues	JSON string document
 *				with notification data.
 * @return			True if the rule was triggered,
 *				false otherwise.
 */
bool OverMaxRule::eval(const string& assetValues)
{
	Document doc;
	doc.Parse(assetValues.c_str());

	if (doc.HasParseError())
	{
		return false;
	}

	return false;
}

/**
 * Return rule trigger reason: trigger or clear the notification. 
 *
 * @return	 A JSON string
 */
string OverMaxRule::reason() const
{
	return "{}";
}
