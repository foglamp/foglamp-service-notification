#ifndef _NOTIFICATION_MANAGER_H
#define _NOTIFICATION_MANAGER_H
/*
 * FogLAMP notification manager.
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */

#include <logger.h>
#include <management_client.h>
#include <rule_plugin.h>
#include <delivery_plugin.h>
#include <notification_service.h>
#include <notification_stats.h>

// Notification type repeat frequency
#define DEFAULT_RETRIGGER_FREQUENCY 60
#define DEFAULT_ONESHOT_FREQUENCY   60
#define DEFAULT_TOGGLE_FREQUENCY    60

/**
 * The EvaluationType class represents
 * the evalutation type of notification data.
 *
 * Supported directives:  Window, Average, Minimum, Maximum
 * with the specified time period
 * and Latest (without time indication)
 * These informations come from "plugin_triggers" call.
 */
class EvaluationType
{
	public:
		typedef enum EvalType {
			Latest,
			Window,
			Average,
			Minimum,
			Maximum
		} EVAL_TYPE;

		EvaluationType(EVAL_TYPE type, time_t interval)
		{
			m_type = type;
			m_interval = interval;
		};
		~EvaluationType() {};

		EVAL_TYPE		getType() const { return m_type; };
		time_t			getInterval() const { return m_interval; };

	private:
		EVAL_TYPE		m_type;
		time_t		m_interval;
		
};

/**
 * This class represents the notification evaluation
 * for a given asset name in a Notification rule.
 */
class NotificationDetail
{
	public:
		NotificationDetail(const std::string& asset,
				   const std::string& rule,
				   EvaluationType& value);
		~NotificationDetail();

		const std::string&	getAssetName() const{ return m_asset; };
		const std::string&	getRuleName() const { return m_rule; };
		const EvaluationType::EVAL_TYPE
					getType() const { return m_value.getType(); };
		const time_t		getInterval() const { return m_value.getInterval(); };

	private:
		std::string		m_asset;
		std::string		m_rule;
		EvaluationType		m_value;
};

/**
 * Parent class for NotificationRule and NotificationDelivery classes
 */
class NotificationElement
{
	public:
		NotificationElement(const std::string& name,
				    const std::string& notification);
		~NotificationElement();
		const std::string&	getName() const { return m_name; };
		const std::string&	getNotificationName() const { return m_notification; };

	private:
		const std::string	m_name;
		const std::string	m_notification;
};

/**
 * The NotificationRule class represents
 * a Notification rule part of a Notification instance.
 * The constructor accepts a builtin rule or a plugin rule name to load.
 *
 * @param    name		The notification rule name set in
 *				the Notifaction instance configuration.
 * @param    notification	The Notification instance name.
 * @param    plugin		The Notification rule, builtin or
 *				a dynamically loaded plugin.
 */
class NotificationRule : public NotificationElement
{
	public:
		NotificationRule(const std::string& name,
				 const std::string& notification,
				 RulePlugin* plugin);
		~NotificationRule();
		RulePlugin*		getPlugin() { return m_plugin; };
		// Get all asset names
		std::vector<NotificationDetail>&
					getAssets() { return m_assets; };
		// Add an asset name
		void			addAsset(NotificationDetail& info)
		{
			m_assets.push_back(info);
		};
		std::string		toJSON();

	private:
		RulePlugin*		m_plugin;
		std::vector<NotificationDetail>
					m_assets;
};

/**
 * The NotificationDelivery class represents
 * a Notification delivery channel part of a Notification instance.
 *
 * @param    name		The notification delivery name set in
 *				the Notifaction instance configuration.
 * @param    notification	The Notification instance name.
 * @param    plugin		The Notification delivery plugin to load.
 */
class NotificationDelivery : public NotificationElement
{
	public:
		NotificationDelivery(const std::string& name,
				     const std::string& notification,
				     DeliveryPlugin* plugin,
				     const std::string& customText);
		~NotificationDelivery();
		DeliveryPlugin*		getPlugin() { return m_plugin; };
		const std::string&	getText() const { return m_text; };
		std::string		toJSON();
		void			setText(const string& text) { m_text = text; };

	private:
		DeliveryPlugin*		m_plugin;
		std::string		m_text;
};

class NotificationInstance
{
	public:
		enum NotificationType { None, OneShot, Retriggered, Toggled };
		enum NotificationState {StateTriggered, StateCleared };
		NotificationInstance(const std::string& name,
				     bool enable,
				     NotificationType type,
				     NotificationRule* rule,
				     NotificationDelivery* delivery);

		~NotificationInstance();

		const std::string&	getName() const { return m_name; };
		NotificationRule*	getRule() { return m_rule; };
		NotificationDelivery*	getDelivery() { return m_delivery; };
		RulePlugin*		getRulePlugin()
		{
			return (m_rule ? m_rule->getPlugin() : NULL);
		};
		DeliveryPlugin*		getDeliveryPlugin()
		{
			return (m_delivery ? m_delivery->getPlugin() : NULL);
		};
		std::string		toJSON();
		bool			isEnabled() const { return m_enable; };
		NotificationType	getType() const { return m_type; };
		std::string		getTypeString(NotificationType type);
		bool			handleState(bool evalRet);
		bool			reconfigure(const std::string& name,
						    const std::string& category);
		bool			updateInstance(const string& name,
						       const ConfigCategory& config);
		void			enable() { m_enable = true; };
		void			disable() { m_enable = false; };
		void			setType(NotificationType type) { m_type = type; }; 
		void			markAsZombie() { m_zombie = true; };
		bool			isZombie() { return m_zombie; };

	private:
		const std::string	m_name;
		bool			m_enable;
		NotificationType	m_type;
		NotificationRule*	m_rule;
		NotificationDelivery*	m_delivery;
		time_t			m_lastSent;
		NotificationState	m_state;
		bool			m_zombie;
};

typedef NotificationInstance::NotificationType NOTIFICATION_TYPE;
typedef std::function<RulePlugin*(const std::string&)> BUILTIN_RULE_FN;

class NotificationManager
{
	public:
		NotificationManager(const std::string& notificationName,
				    ManagementClient* managerClient,
				    NotificationService* service);
		~NotificationManager();

		const std::string&	getName() const { return m_name; };
		static NotificationManager*
					getInstance();
		std::string		getJSONInstances() const;
		void 			loadInstances();
		std::map<std::string, NotificationInstance *>&
					getInstances() { return m_instances; };
		NotificationInstance*	getNotificationInstance(const std::string& instanceName) const;
		NOTIFICATION_TYPE	parseType(const std::string& type);
		std::string		getJSONRules();
		std::string		getJSONDelivery();
		bool			APIcreateEmptyInstance(const std::string& name);
		RulePlugin*		createRuleCategory(const std::string& name,
							   const std::string& rule);
		 DeliveryPlugin*	createDeliveryCategory(const std::string& name,
							       const std::string& delivery);
		std::string		getPluginInfo(PLUGIN_INFORMATION* info);
		bool			createInstance(const std::string& name,
						       const std::string& category);
		bool			setupInstance(const string& name,
						      const ConfigCategory& config);
		bool			removeInstance(const string& instanceName);
		void			lockInstances() { m_instancesMutex.lock(); };
		void			unlockInstances() { m_instancesMutex.unlock(); };
		bool			getConfigurationItems(const ConfigCategory& config,
							      bool& enable,
							      std::string& rulePluginName,
							      std::string& deliveryPluginName,
							      NOTIFICATION_TYPE& type,
							      std::string& customText);
		bool			auditNotification(const std::string& notification);
		bool			APIdeleteInstance(const string& instanceName);
		void			updateSentStats() { m_stats.sent++; };
		void			collectZombies();

	private:
		PLUGIN_HANDLE		loadRulePlugin(const std::string& rulePluginName);
		PLUGIN_HANDLE		loadDeliveryPlugin(const std::string& deliveryPluginName);
		RulePlugin*		findBuiltinRule(const std::string& rulePluginName);
		template<typename T> void
					registerBuiltinRule(const std::string& ruleName);
		void			addInstance(const std::string& instanceName,
						    bool enable,
						    NOTIFICATION_TYPE type,
						    NotificationRule* rule,
						    NotificationDelivery* delivery);
		RulePlugin*		createRulePlugin(const std::string& rulePluginName);
		DeliveryPlugin*		createDeliveryPlugin(const std::string& deliveryPluginName);

	private:
		const std::string	m_name;
		static NotificationManager*
					m_instance;
		ManagementClient* 	m_managerClient;
		std::map<std::string, NotificationInstance *>
					m_instances;
		std::map<std::string, BUILTIN_RULE_FN>
					m_builtinRules;
		NotificationService*	m_service;
		Logger*			m_logger;
		std::mutex		m_instancesMutex;
		NotificationStats	m_stats;
};
#endif
