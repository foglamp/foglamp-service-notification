/**
 * FogLAMP Threshold builtin notification rule
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */

#include <threshold_rule.h>

#define TO_STRING(...) DEFER(TO_STRING_)(__VA_ARGS__)
#define DEFER(x) x
#define TO_STRING_(...) #__VA_ARGS__
#define QUOTE(...) TO_STRING(__VA_ARGS__)

#define RULE_NAME "Threshold"
#define DEFAULT_TIME_INTERVAL "30"

/**
 * Rule specific default configuration
 */
static const char *default_config = QUOTE({
			"plugin": {
				"description": "The threshold notification rule plugin triggers a notification when reading data exceed an absolute limit value.", 
				"type": "string",
				"default": RULE_NAME,
				"order": "8",
				"displayName" : "The threshold notification rule plugin triggers a notification when reading data exceed an absolute limit value.", 
				"readonly": "true"
				},
			"description": {
				"description": "Generate a notification when datapoint value crosses a boundary.",
				"type": "string",
				"default": "Generate a notification if the value of a configured datapoint within an asset name exceeds a configured value.",
				"displayName" : "Rule",
				"order": "1"
				},
			"asset" : {
				"description": "The asset name for which notifications will be generated.",
				"type": "string",
				"default": "",
				"displayName" : "Asset name",
				"order": "2"
				},
			"datapoint" : {
				"description": "The datapoint within the asset name for which notifications will be generated.",
				"type": "string",
				"default": "",
				"displayName" : "Datapoint name",
				"order": "3"
				},
			"condition" : {
				"description": "The condition to evalaute", 
				"type": "enumeration",
				"options": [ ">", ">=", "<", "<=" ],
				"default" : ">",
				"displayName" : "Condition",
				"order": "4"
				},
			"trigger_value" : {
				"description": "Value at which to trigger a notification.",
				"type": "float",
				"default": "0.0",
				"displayName" : "Trigger value",
				"order": "5"
				},
			"evaluation_data": {
				"description": "The rule evaluation data: single item or window", 
				"type": "enumeration",
				"options": [ "Single Item", "Window"],
				"default" : "Single Item",
				"displayName" : "Evaluation data",
				"order": "6"
				},
			"window_data": {
				"description": "Window data evaluation type",
				"type": "enumeration",
				"options": [ "Maximum", "Minimum", "Average"],
				"default" : "Average",
				"displayName" : "Window evaluation",
				"order": "7"
				},
			"time_window" : {
				"description": "Duration of the time window, in seconds, for collecting data points",
				"type": "integer",
				"default": DEFAULT_TIME_INTERVAL, 
				"displayName" : "Time window",
				"order": "8"
				}
	});


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
	default_config			// Configuration
};

/**
 * ThresholdRule builtin rule constructor
 *
 * Call parent class RulePlugin constructor
 * passing a NULL plugin handle 
 *
 * @param    name	The builtin rule name
 */
ThresholdRule::ThresholdRule(const std::string& name) :
			 RulePlugin(name, NULL)
{
}

/**
 * ThresholdRule builtin rule destructor
 */
ThresholdRule::~ThresholdRule()
{
}

/**
 * Return rule info
 */
PLUGIN_INFORMATION* ThresholdRule::getInfo()
{       
	return &ruleInfo;
}

/**
 * Initialise rule objects based in configuration
 *
 * @param    config	The rule configuration category data.
 * @return		The rule handle.
 */
PLUGIN_HANDLE ThresholdRule::init(const ConfigCategory& config)
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
void ThresholdRule::shutdown()
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
string ThresholdRule::triggers()
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
bool ThresholdRule::eval(const string& assetValues)
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
string ThresholdRule::reason() const
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
void ThresholdRule::reconfigure(const string& newConfig)
{
	ConfigCategory  config("threshold", newConfig);
	this->configure(config);
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
bool ThresholdRule::checkLimit(const Value& point,
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
					switch (m_condition)
					{
						case THRESHOLD_GREATER:
							ret = point.GetInt() > limitValue;
							break;
						case THRESHOLD_GREATER_EQUAL:
							ret = point.GetInt() >= limitValue;
							break;
						case THRESHOLD_LESS:
							ret = point.GetInt() < limitValue;
							break;
						case THRESHOLD_LESS_EQUAL:
							ret = point.GetInt() <= limitValue;
							break;

					}
				}
				else
				{
					switch (m_condition)
					{
						case THRESHOLD_GREATER:
							ret = point.GetInt64() > limitValue;
							break;
						case THRESHOLD_GREATER_EQUAL:
							ret = point.GetInt64() >= limitValue;
							break;
						case THRESHOLD_LESS:
							ret = point.GetInt64() < limitValue;
							break;
						case THRESHOLD_LESS_EQUAL:
							ret = point.GetInt64() <= limitValue;
							break;

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
bool ThresholdRule::evalAsset(const Value& assetValue,
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
				assetEval = checkLimit(point, (*it)->getData().toDouble());
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
void ThresholdRule::configure(const ConfigCategory& config)
{
	BuiltinRule* handle = (BuiltinRule *)m_instance;
	string assetName = config.getValue("asset");
	string dataPointName = config.getValue("datapoint");

	if (!assetName.empty() &&
	    !dataPointName.empty())
	{
		// evaluation_type can be empty, it means SingleItem values
		string evaluation_data;
		// time_window might be not present only
		// if evaluation_type is empty
		unsigned int timeInterval = atoi(DEFAULT_TIME_INTERVAL);

		if (config.itemExists("evaluation_data"))
		{
			evaluation_data = config.getValue("evaluation_data");
			if (evaluation_data.compare("Single Item") == 0)
			{
				evaluation_data.clear();
				timeInterval = 0;
			}
			else
			{
				if (config.itemExists("window_data"))
				{
					evaluation_data = config.getValue("window_data");
				}

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
			pTrigger->addEvaluation(evaluation_data,
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
	string condition = config.getValue("condition");
	if (condition.compare(">") == 0)
		m_condition = THRESHOLD_GREATER;
	else if (condition.compare(">=") == 0)
		m_condition = THRESHOLD_GREATER_EQUAL;
	else if (condition.compare("<") == 0)
		m_condition = THRESHOLD_LESS;
	else if (condition.compare("<=") == 0)
		m_condition = THRESHOLD_LESS_EQUAL;
}
