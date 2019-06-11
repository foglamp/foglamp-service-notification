#include <gtest/gtest.h>
#include "notification_service.h"
#include "notification_manager.h"
#include "notification_queue.h"
#include <dirent.h>

using namespace std;

TEST(NotificationService, GetPlugins)
{
EXPECT_EXIT({
	string myName = "myName";

	ManagementClient* managerClient = new ManagementClient("0.0.0.0", 0);
	NotificationManager instances(myName, managerClient, NULL);

	// Check for embedded OverMaxRule rule plugin
	bool ret = instances.getJSONRules().find("OverMaxRule") != string::npos;
	if (!ret)
	{
		delete managerClient;
		cerr << "Embedded OverMaxRule rule plugin not found" << endl;
		exit(1);
	}

	// If FOGLAMP_ROOT is not set we should no delivery plugins
	if (!getenv("FOGLAMP_ROOT"))
	{
		ret = instances.getJSONDelivery().compare("[]") == 0;
		if (!ret)
		{
			cerr << "Delivery plugin array must be empty without FOGLAMP_ROOT" << endl;
		}
	}
	else
	{
		// If FOGLAMP_ROOT is set we should have some delivery plugins
		string deliveryPluginDir(getenv("FOGLAMP_ROOT"));
		deliveryPluginDir += "/plugins/notificationDelivery";
		DIR* dir = opendir(deliveryPluginDir.c_str());
		bool pluginsFound = false;
		if (dir)
		{
			struct dirent *entry;
			while ((entry = readdir(dir)))
			{
				if (strcmp (entry->d_name, "..") != 0 &&
				    strcmp (entry->d_name, ".") != 0)
				{
					pluginsFound = true;
				}
			}
			closedir(dir);
		}

		if (pluginsFound)
		{
			// Check array is NOT empty
			ret = instances.getJSONDelivery().compare("[]") != 0;
			if (!ret)
			{
				cerr << "Delivery plugin array can not be empty with found plugins" << endl;
			}
		}
		else
		{
			// Check array must be empty
			ret = instances.getJSONDelivery().compare("[]") == 0;
			if (!ret)
			{
				cerr << "Delivery plugin array has to be empty without found plugins" << endl;
			}
		}
	}

	delete managerClient;

	exit(!(ret == true)); }, ::testing::ExitedWithCode(0), "");
}
