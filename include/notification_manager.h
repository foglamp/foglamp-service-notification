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

class NotificationRule : public NotificationElement
{
	public:
		NotificationRule(const std::string& name,
				 const std::string& notification,
				 RulePlugin* plugin);
		~NotificationRule();
		RulePlugin*		getPlugin() { return m_plugin; };

	private:
		RulePlugin*		m_plugin;
};

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

	private:
		DeliveryPlugin*		m_plugin;
		std::string		m_text;
};

class NotificationInstance
{
	public:
		enum NotificationType { None, OneShot, Retriggered, Toggled };
		NotificationInstance(const std::string& name,
				     bool enable,
				     NotificationType type,
				     NotificationRule* rule,
				     NotificationDelivery* delivery);

		~NotificationInstance();

		const std::string&	getName() const { return m_name; };
		NotificationRule*	getRule() const { return m_rule; };
		NotificationDelivery*	getDelivery() const { return m_delivery; };
		RulePlugin*		getRulePlugin() const
		{
			return (m_rule ? m_rule->getPlugin() : NULL);
		};
		DeliveryPlugin*		getDeliveryPlugin() const
		{
			return (m_delivery ? m_delivery->getPlugin() : NULL);
		};
		string			toJSON();
		bool			isEnabled() const { return m_enable; };
		NotificationType	getType() const { return m_type; };
		string			getTypeString(NotificationType type);

	private:
		const std::string	m_name;
		bool			m_enable;
		NotificationType	m_type;
		NotificationRule*	m_rule;
		NotificationDelivery*	m_delivery;
};

typedef NotificationInstance::NotificationType NOTIFICATION_TYPE;
typedef std::function<RulePlugin*(const std::string&)> BUILTIN_RULE_FN;

class NotificationManager
{
	public:
		NotificationManager(const std::string& notificationName,
				    ManagementClient* managerClient);
		~NotificationManager();

		const std::string&	getName() const { return m_name; };
		static NotificationManager*
					getInstance();
		std::string		getJSONInstances() const;
		bool 			loadInstances();
		std::map<std::string, NotificationInstance *>&
					getInstances() { return m_instances; };
		NotificationInstance*	getNotificationInstance(const std::string& instanceName) const;
		NOTIFICATION_TYPE	parseType(const string& type);
		RulePlugin*		createRulePlugin(const string& rulePluginName);
		DeliveryPlugin*		createDeliveryPlugin(const string& deliveryPluginName);

	private:
		PLUGIN_HANDLE		loadRulePlugin(const string& rulePluginName);
		PLUGIN_HANDLE		loadDeliveryPlugin(const string& deliveryPluginName);
		RulePlugin*		findBuiltinRule(const string& rulePluginName);
		template<typename T> void registerBuiltinRule(const std::string& ruleName);
		void			addInstance(const string& instanceName,
						    bool enable,
						    NOTIFICATION_TYPE type,
						    NotificationRule* rule,
						    NotificationDelivery* delivery);

	private:
		const std::string	m_name;
		static NotificationManager*
					m_instance;
		ManagementClient* 	m_managerClient;
		std::map<std::string, NotificationInstance *>
					m_instances;
		std::map<std::string, BUILTIN_RULE_FN>
					m_builtinRules;
};
#endif
