#ifndef _BUILTIN_RULE_H
#define _BUILTIN_RULE_H
/*
 * FogLAMP Builtin rule class.
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
#include <management_client.h>
#include <rule_plugin.h>

/**
 * This class represents the basic notification trigger:
 * for given asset name we set evaluation_type,
 * time intervsl and needed datapoints with a
 * configured single value
 *
 * This can be extended with a double member tolerance
 * for a rule which checks toleance instead of max/min value.
 * It can also be extended to support lowe bound and upper bound limits
 * for a rule that needs a datapoints in the set limits
 */
class RuleTrigger
{
	public:
		RuleTrigger(const std::string& name,
			    Datapoint* datapoint)
		{
			m_datapoints.push_back(datapoint);
		};
		~RuleTrigger()
		{
			for (auto d = m_datapoints.begin();
				  d != m_datapoints.end();
				  ++d)
			{
				delete *d;
			}

		};

		std::string&		getAsset() { return m_asset; };
		void addEvaluation(const std::string& evaluation_type,
				   unsigned int timeInterval,
				   bool evalAllDatapoints)
		{
			m_evaluation = evaluation_type;
			m_interval = timeInterval;
			m_evalAll = evalAllDatapoints;
		};
		std::string&		getEvaluation() { return m_evaluation; };
		unsigned int		getInterval() { return m_interval; };
		std::vector<Datapoint*>&
					getDatapoints() { return m_datapoints; };
		bool			evalAllDatapoints() const { return m_evalAll; };

	private:
		std::string			m_asset;
		std::vector<Datapoint*>		m_datapoints;
		std::string			m_evaluation;
		unsigned int 			m_interval;
		bool				m_evalAll;
};

/**
 * This class represents the builtin notification rule,
 * with all needed triggers and rule evaluation state
 */
class BuiltinRule
{
	public:
		typedef enum { StateCleared, StateTriggered } TRIGGER_STATE;
		BuiltinRule() { m_state = StateCleared; };
		~BuiltinRule()
		{
			// Delete all triggers
			removeTriggers();
		};

		void		addTrigger(const std::string& asset,
					   RuleTrigger* trigger)
		{
			m_triggers.insert(std::pair<std::string,
					  RuleTrigger *>(asset, trigger));
		};
		void		removeTriggers()
				{
					// Delete all triggers
					for (auto r = m_triggers.begin();
						  r != m_triggers.end();
						  ++r)
					{
						delete (*r).second;
					}

					m_triggers.clear();
				};

		bool		hasTriggers() const { return m_triggers.size() != 0; };
		std::map<std::string, RuleTrigger *>&
				getTriggers() { return m_triggers; };
		void		setState(bool evalResult)
		{
			m_state = evalResult ?
				  BuiltinRule::StateTriggered :
				  BuiltinRule::StateCleared;
		};
		TRIGGER_STATE	getState() const { return m_state; };

	private:
		TRIGGER_STATE		m_state;
		std::map<std::string, RuleTrigger *>
					m_triggers;
		
};

#endif
