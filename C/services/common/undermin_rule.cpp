/**
 * FogLAMP UnderMin builtin notification rule
 *
 * Copyright (c) 2019 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Mark Riddoch
 */

#include <undermin_rule.h>

#define RULE_NAME "UnderMinRule"
#define DEFAULT_TIME_INTERVAL "30"

/**
 * Rule specific default configuration
 */
#define RULE_DEFAULT_CONFIG \
			"\"asset\" : { " \
				"\"description\": \"The asset name for which " \
					"notifications will be generated.\", " \
				"\"type\": \"string\", " \
				"\"default\": \"\", " \
				"\"displayName\" : \"Asset name\", " \
				"\"order\": \"1\" }, " \
			"\"datapoint\" : { " \
				"\"description\": \"The datapoint within the asset name " \
					"for which notifications will be generated.\", " \
				"\"type\": \"string\", " \
				"\"default\": \"\", " \
				"\"displayName\" : \"Datapoint\", " \
				"\"order\": \"2\" }, " \
			"\"evaluation_type\": {" \
				"\"description\": \"The rule evaluation type\", " \
				"\"type\": \"enumeration\", " \
					"\"options\": [ " \
					"\"window\", \"maximum\", \"minimum\", \"average\", \"latest\" ], " \
				"\"default\" : \"latest\", " \
				"\"displayName\" : \"Evaluation type\", \"order\": \"3\" }, " \
			"\"time_window\" : { " \
				"\"description\": \"Duration of the time window, in seconds, " \
					"for collecting data points except for 'latest' evaluation.\", " \
				"\"type\": \"integer\" , " \
				"\"default\": \"" DEFAULT_TIME_INTERVAL "\", " \
				"\"displayName\" : \"Time Window\", " \
				"\"order\": \"4\" }, " \
			"\"trigger_value\" : { " \
				"\"description\": \"Value at which to trigger a notification.\", " \
				"\"type\": \"float\" , " \
				"\"default\": \"0.0\", " \
				"\"displayName\" : \"Trigger Value\", " \
				"\"order\": \"5\" }"

#define BUITIN_RULE_DESC "\"plugin\": {\"description\": \"The " RULE_NAME " notification rule plugin " \
					"triggers a notification when reading data drops below an absolute threshold value.\", " \
				"\"type\": \"string\", \"default\": \"" RULE_NAME "\", " \
				"\"readonly\": \"true\"}"

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
 * UnderMinRule builtin rule constructor
 *
 * Call parent class RulePlugin constructor
 * passing a NULL plugin handle 
 *
 * @param    name	The builtin rule name
 */
UnderMinRule::UnderMinRule(const std::string& name) :
			 RulePlugin(name, NULL)
{
}

/**
 * UnderMinRule builtin rule destructor
 */
UnderMinRule::~UnderMinRule()
{
}

/**
 * Return rule info
 */
PLUGIN_INFORMATION* UnderMinRule::getInfo()
{       
	return &ruleInfo;
}

/**
 * Initialise rule objects based in configuration
 *
 * @param    config	The rule configuration category data.
 * @return		The rule handle.
 */
PLUGIN_HANDLE UnderMinRule::init(const ConfigCategory& config)
{
	BuiltinRule* builtinRule = new BuiltinRule();
	m_instance = (PLUGIN_HANDLE)builtinRule;

	// Configure plugin
	this->configure(config);

	return (m_instance ? &m_instance : NULL);
}

/**
 * Free rule resources
 */
void UnderMinRule::shutdown()
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
string UnderMinRule::triggers()
{
	string ret;
	BuiltinRule* handle = (BuiltinRule *)m_instance;
	// Configuration fetch is protected by a lock
	lock_guard<mutex> guard(m_configMutex);

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
 * @param    assetValues	JSON string document
 *				with notification data.
 * @return			True if the rule was triggered,
 *				false otherwise.
 */
bool UnderMinRule::eval(const string& assetValues)
{
	Document doc;
	doc.Parse(assetValues.c_str());
	if (doc.HasParseError())
	{
		return false;
	}

	bool eval = false; 
	BuiltinRule* handle = (BuiltinRule *)m_instance;
	// Configuration fetch is protected by a lock
	lock_guard<mutex> guard(m_configMutex);

	map<std::string, RuleTrigger *>& triggers = handle->getTriggers();

	// Iterate throgh all configured assets
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
			// Get all datapoints for assetName
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
string UnderMinRule::reason() const
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
 * @param    newConfig		The new configuration for the plugin
 */
void UnderMinRule::reconfigure(const string& newConfig)
{
	ConfigCategory  config("overmax", newConfig);
	this->configure(config);
}

/**
 * Check whether the input datapoint
 * is a NUMBER and its value is greater than configured DOUBLE limit
 *
 * @param    name		The datapoint name, just for error logging
 * @param    point		Current input datapoint
 * @param    limitValue		The DOUBLE limit value
 * @return			True if limit is hit,
 *				false otherwise
 */
bool UnderMinRule::checkLimit(const string&name,
			      const Value& point,
			      double limitValue)
{
	bool ret = false;

	// Check config datapoint type
	if (point.GetType() == kNumberType)
	{
		ret = this->evalDatapoint(name, point, limitValue);
	}
	else if (point.GetType() == kArrayType)
	{
		// Array of values: get all values and
		// return after first successfull evaluation
		for (auto& v : point.GetArray())
		{
			ret = this->evalDatapoint(name, v, limitValue);
			if (ret)
			{
				break;
			}
		}
	}
	else
        {
		Logger::getLogger()->warn("%s: datapoint %s has unsupported data type of %s",
					  RULE_NAME,
					  name.c_str(),
					  kTypeNames[point.GetType()]);
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
bool UnderMinRule::evalAsset(const Value& assetValue,
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
			if ((*it)->getData().getType() == DatapointValue::T_FLOAT)
			{
				assetEval = checkLimit(datapointName,
							point,
							(*it)->getData().toDouble());
			}
			else
			{
				assetEval = false;
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

/**
 * Configure the builtin rule plugin
 *
 * @param    config	The configuration object to process
 */
void UnderMinRule::configure(const ConfigCategory& config)
{
	BuiltinRule* handle = (BuiltinRule *)m_instance;
	string assetName = config.getValue("asset");
	string dataPointName = config.getValue("datapoint");

	if (!assetName.empty() &&
	    !dataPointName.empty())
	{
		// evaluation_type can be empty, it means latest value
		string evaluation_type;
		// time_window might be not present only
		// if evaluation_type is empty
		unsigned int timeInterval = atoi(DEFAULT_TIME_INTERVAL);

		if (config.itemExists("evaluation_type"))
		{
			evaluation_type = config.getValue("evaluation_type");
			if (evaluation_type.compare("latest") == 0)
			{
				evaluation_type.clear();
				timeInterval = 0;
			}
			else
			{
				if (config.itemExists("time_window"))
				{
					timeInterval = atoi(config.getValue("time_window").c_str());
				}
			}
		}

		if (config.itemExists("trigger_value"))
		{
			double maxVal = atof(config.getValue("trigger_value").c_str());
			DatapointValue value(maxVal);
			Datapoint* point = new Datapoint(dataPointName, value);
			RuleTrigger* pTrigger = new RuleTrigger(dataPointName, point);
			pTrigger->addEvaluation(evaluation_type,
						timeInterval,
						false);

			// Configuration change is protected by a lock
			lock_guard<mutex> guard(m_configMutex);

			if (handle->hasTriggers())
			{
				handle->removeTriggers();
			}
			handle->addTrigger(assetName, pTrigger);
		}
		else
		{
			Logger::getLogger()->error("Builtin rule %s configuration error: "
						   "required parameter 'trigger_value' not found",
						   RULE_NAME);
		}
	}
}

/**
 * Eval JSON data against limit value
 *
 * @param    name		The datapoint name, just for error logging
 * @param    point		Input data: single value or array of values
 * @param    limitValue		The limit value for inout data
 * @return			True is data value is below the limit value,
 *				false otherwise
 */
bool UnderMinRule::evalDatapoint(const string& name,
				 const Value& point,
				 double limitValue)
{
	bool ret = false;
	if (point.IsDouble())
	{
		if (point.GetDouble() < limitValue)
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
				if (point.GetInt() < limitValue)
				{
					ret = true;
				}
			}
			else
			{
				if (point.GetInt64() < limitValue)
				{
					ret = true;
				}
			}
		}
		else
		{
			Logger::getLogger()->warn("%s: datapoint %s has unsupported type of %s",
						   RULE_NAME,
						   name.c_str(),
						   kTypeNames[point.GetType()]);
		}
	}

	return ret;
}
