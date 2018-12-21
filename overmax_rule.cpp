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

#define RULE_NAME "OverMaxRule"

/**
 * Rule specific default configuration
 */
#define RULE_DEFAULT_CONFIG \
			"\"description\": { " \
				"\"description\": \"Builtin " RULE_NAME " notification rule\", " \
				"\"type\": \"string\", " \
				"\"default\": \"The value of a reading data exceeds an absolute limit value\", " \
				"\"order\": \"1\" }, " \
			"\"rule_config\": { " \
				"\"description\": \"Builtin " RULE_NAME " configuration\", " \
				"\"type\": \"JSON\", " \
				"\"default\": \"{\\\"rules\\\" : [" \
							"{ \\\"asset\\\" : {" \
								"\\\"description\\\" : \\\"The asset name receiving notifications\\\", " \
								"\\\"name\\\" : \\\"\\\" }, " \
							   "\\\"evaluation_type\\\": {" \
								"\\\"description\\\": \\\"Rule evaluation type\\\", " \
								"\\\"type\\\": \\\"enumeration\\\", " \
									"\\\"options\\\": [ " \
										"\\\"window\\\", \\\"maximum\\\", " \
										"\\\"minimum\\\", \\\"average\\\" " \
									"], \\\"value\\\": \\\"\\\" }, " \
							   "\\\"eval_all_datapoints\\\" : true, " \
							   "\\\"datapoints\\\": [ {\\\"name\\\": \\\"\\\", " \
									"\\\"type\\\": \\\"integer\\\", " \
									"\\\"max_allowed_value\\\": 0} ] } ] }\", " \
				"\"displayName\" : \"" RULE_NAME " configuration\", " \
				"\"order\": \"2\" }"

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

	BuiltinRule* builtinRule = new BuiltinRule();

	string JSONrules = config.getValue("rule_config");

	Document doc;
	doc.Parse(JSONrules.c_str());

	if (!doc.HasParseError())
	{
		if (doc.HasMember("rules"))
		{
			// Gwt defined rules
			const Value& rules = doc["rules"];
			if (rules.IsArray())
			{
				/**
				 * For each rule fetch:
				 * asset: name,
				 * evaluation_type: value
				 * time_interval
				 * datapoints array with max_allowed_value
				 * eval_all_datapoins: check all datapoint values
				 * or just eval the rule for at least one datapoint
				 */
				for (auto& rule : rules.GetArray())
				{
					if (!rule.HasMember("asset") && !rule.HasMember("datapoints"))
					{
						continue;
					}

					const Value& asset = rule["asset"];
					string assetName = asset["name"].GetString();
					if (assetName.empty())
					{
						continue;
					}
					// evaluation_type can be empty, it means latest value
					string evaluation_type;
					// time_interval might be not present only
					// if evaluation_type is empty
					unsigned int timeInterval = 0;
					if (rule.HasMember("evaluation_type"))
					{
						const Value& type = rule["evaluation_type"];
						evaluation_type = type["value"].GetString();
						if (!evaluation_type.empty() &&
						    rule.HasMember("time_interval"))
						{
							const Value& interval = rule["time_interval"];
							timeInterval = interval.GetInt();
						}
						else
						{
							// Log message
						}
					}

					const Value& datapoints = rule["datapoints"];
					bool evalAlldatapoints = true;
					bool foundDatapoints = false;
					if (rule.HasMember("eval_all_datapoints") &&
					    rule["eval_all_datapoints"].IsBool())
					{
						evalAlldatapoints = rule["eval_all_datapoints"].GetBool();
					}

					if (datapoints.IsArray())
					{
						for (auto& d : datapoints.GetArray())
						{
							if (d.HasMember("name"))
							{
								foundDatapoints = true;

								string dataPointName = d["name"].GetString();
								// max_allowed_value is specific for this rule
								if (d.HasMember("max_allowed_value") &&
								    d["max_allowed_value"].IsInt())
								{
									DatapointValue value((long)d["max_allowed_value"].GetInt());
									Datapoint* point = new Datapoint(dataPointName, value);
									RuleTrigger* pTrigger = new RuleTrigger(dataPointName, point);
									pTrigger->addEvaluation(evaluation_type,
												timeInterval,
												evalAlldatapoints);
									builtinRule->addTrigger(assetName, pTrigger);
								}
							}
						}
					}
					if (!foundDatapoints)
					{
						// Log message
					}
				}
			}
		}
	}

	m_instance = (PLUGIN_HANDLE)builtinRule;

	return (m_instance ? &m_instance : NULL);
}

/**
 * Free rule resources
 */
void OverMaxRule::shutdown()
{
	BuiltinRule* handle = (BuiltinRule *)m_instance;
	// Delete plugin handle
	delete handle;
}

/**
 * Return triggers JSON document
 *
 * @return	JSON string
 */
string OverMaxRule::triggers() const
{
	string ret;
	BuiltinRule* handle = (BuiltinRule *)m_instance;
	if (!handle->hasTriggers())
	{
		ret = "{\"triggers\" : []}";
		return ret;
	}

	ret = "{\"triggers\" : [ ";
	std::map<std::string, RuleTrigger *> triggers = handle->getTriggers();
	for (auto it = triggers.begin();
		  it != triggers.end();
		  ++it)
	{
		ret += "{ \"asset\"  : \"" + (*it).first + "\"";
		if (!(*it).second->getEvaluation().empty())
		{
			ret += ", \"" + (*it).second->getEvaluation() + "\" : " + \
				to_string((*it).second->getInterval()) + " }";
		}
		else
		{
			ret += " }";
		}
		
		if (std::next(it, 1) != triggers.end())
		{
			ret += ", ";
		}
	}

	ret += " ] }";

	return ret;
}

/**
 * Evaluate notification data received
 *
 *  Note: all assets must trigger in order to return TRUE
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

	bool eval = false; 
	BuiltinRule* handle = (BuiltinRule *)m_instance;
	map<std::string, RuleTrigger *>& triggers = handle->getTriggers();

	// Iterate throgh all configured assets
	// If we have multiple asset the evaluation result is
	// TRUE only if all assets checks returned true
	for (auto t = triggers.begin();
		  t != triggers.end();
		  ++t)
	{
		string assetName = (*t).first;
		if (!doc.HasMember(assetName.c_str()))
		{
			eval = false;
		}
		else
		{
			// Get all datapoints fir assetName
			const Value& assetValue = doc[assetName.c_str()];

			// Set evaluation
			eval = this->evalAsset(assetValue, (*t).second);
		}
	}

	// Set final state: true is all calls to evalAsset() returned true
	handle->setState(eval);
	
	return eval;
}

/**
 * Return rule trigger reason: trigger or clear the notification. 
 *
 * @return	 A JSON string
 */
string OverMaxRule::reason() const
{
	BuiltinRule* handle = (BuiltinRule *)m_instance;

	string ret = "{ \"reason\": \"";
	ret += handle->getState() == BuiltinRule::StateTriggered ? "triggered" : "cleared";
	ret += "\" }";

	return ret;
}

/**
 * Call the reconfigure method in the plugin
 *
 * Not implemented yet
 *
 * @param    newConfig		The new configuration for the plugin
 */
void OverMaxRule::reconfigure(const string& newConfig)
{
}

/**
 * Check whether the input datapoint
 * is a NUMBER and its value is greater than configured LONG limit
 *
 * @param    point		Current input datapoint
 * @param    limitValue		The LONG limit value
 * @return			True if limit is hit,
 *				false otherwise
 */
bool OverMaxRule::checkLongLimit(const Value& point,
				 long limitValue)
{
	bool ret = false;

	// Check config datapoint type
	if (point.GetType() == kNumberType)
	{
		if (point.IsDouble())
		{       
			if (point.GetDouble() > limitValue)
			{       
				ret = true;
			}
		}
		else
		{
  			if (point.IsInt() ||
			    point.IsUint() ||
			    point.IsInt64() ||
			    point.IsUint64())
			{
				if (point.IsInt() ||
				    point.IsUint())
				{
					if (point.GetInt() > limitValue)
					{
						ret = true;
					}
				}
				else
				{
					if (point.GetInt64() > limitValue)
					{
						ret = true;
					}
				}
			}
		}
	}

	return ret;
}

/**
 * Check whether the input datapoint
 * is a NUMBER and its value is greater than configured DOUBLE limit
 *
 * @param    point		Current input datapoint
 * @param    limitValue		The DOUBLE limit value
 * @return			True if limit is hit,
 *				false otherwise
 */
bool OverMaxRule::checkDoubleLimit(const Value& point,
				   double limitValue)
{
	bool ret = false;

	// Check config datapoint type
	if (point.GetType() == kNumberType)
	{
		if (point.IsDouble())
		{       
			if (point.GetDouble() > limitValue)
			{       
				ret = true;
			}
		}
		else
		{
  			if (point.IsInt() ||
			    point.IsUint() ||
			    point.IsInt64() ||
			    point.IsUint64())
			{
				if (point.IsInt() ||
				    point.IsUint())
				{
					if (point.GetInt() > limitValue)
					{
						ret = true;
					}
				}
				else
				{
					if (point.GetInt64() > limitValue)
					{
						ret = true;
					}
				}
			}
		}
	}

	return ret;
}

/**
 * Evaluate datapoints values for the given asset name
 *
 * @param    assetValue		JSON object with datapoints
 * @param    rule		Current configured rule trigger.
 *
 * @return			True if evalution succeded,
 *				false otherwise.
 */
bool OverMaxRule::evalAsset(const Value& assetValue,
			    RuleTrigger* rule)
{
	bool assetEval = false;

	bool evalAlldatapoints = rule->evalAllDatapoints();
	// Check all configured datapoints for current assetName
	vector<Datapoint *> datapoints = rule->getDatapoints();
	for (auto it = datapoints.begin();
		  it != datapoints.end();
	 	 ++it)
	{
		string datapointName = (*it)->getName();
		// Get input datapoint name
		if (assetValue.HasMember(datapointName.c_str()))
		{
			const Value& point = assetValue[datapointName.c_str()];
			// Check configuration datapoint type
			switch ((*it)->getData().getType())
			{
			case DatapointValue::T_INTEGER:
				assetEval = checkLongLimit(point,
							   (*it)->getData().toInt());
				break;
			case DatapointValue::T_FLOAT:
				assetEval = checkDoubleLimit(point,
							   (*it)->getData().toInt());
				break;
			case DatapointValue::T_STRING:
			default:
				break;
				assetEval = false;
			}

			// Check eval all datapoints
			if (assetEval == true &&
			    evalAlldatapoints == false)
			{
				// At least one datapoint has been evaluated
				break;
			}
		}
		else
		{
			assetEval = false;
		}
	}

	// Return evaluation for current asset
	return assetEval;
}
