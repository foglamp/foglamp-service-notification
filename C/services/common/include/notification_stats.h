#ifndef _NOTIFICATION_STATS_H
#define _NOTIFICATION_STATS_H
/*
 * Fledge Notification service statistics
 *
 * Copyright (c) 2019 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */

#include <json_provider.h>
#include <string>
#include <sstream>

class NotificationStats : public JSONProvider {
	public:
		NotificationStats()
		{
			loaded = 0;
			created = 0;
			removed = 0;
			total = 0;
			sent = 0;
		};
		void	asJSON(std::string& json) const
		{
			std::ostringstream convert;

			convert << "{ \"sentNotifications\" : " << sent << ", ";
			convert << "\"loadedInstances\" : " << loaded << ", ";
			convert << "\"createdInstances\" : " << created << ", ";
			convert << "\"removedInstances\" : " << removed << ", ";
			convert << "\"totalInstances\" : " << total << " }";

			json = convert.str();
		};

	public:
		unsigned int	sent;		// Sent notifications
		unsigned int	created;	// Created instances via API
		unsigned int	removed;	// Removed instances via API
		unsigned int	loaded;		// Loaded instances
						// found in Notifications category
		unsigned int	total;		// Total instances
};
#endif
