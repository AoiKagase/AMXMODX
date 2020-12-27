// vim: set ts=4 sw=4 tw=99 noet:
//
// AMX Mod X, based on AMX Mod by Aleksander Naszko ("OLO").
// Copyright (C) The AMX Mod X Development Team.
//
// This software is licensed under the GNU General Public License, version 3 or higher.
// Additional exceptions apply. For full license details, see LICENSE.txt or visit:
//     https://alliedmods.net/amxmodx-license

#include "amxmodx.h"
#include "CoreConfig.h"
#include "CLibrarySys.h"
#include <amxmodx_version.h>

CoreConfig CoreCfg;

const char *MainConfigFile = "amxx.cfg";
const char *AutoConfigDir  = "/plugins";
const char *MapConfigDir   = "/maps";
const char *CommandFormat  = "exec %s\n";

CoreConfig::CoreConfig()
{
	Clear();
}

CoreConfig::~CoreConfig()
{
}

void CoreConfig::OnAmxxInitialized()
{
	m_ConfigsBufferedForward = registerForward("OnAutoConfigsBuffered", ET_IGNORE, FP_DONE);
	m_ConfigsExecutedForward = registerForward("OnConfigsExecuted", ET_IGNORE, FP_DONE);
}

void CoreConfig::Clear()
{
	m_ConfigsExecuted = false;
	m_PendingForwardPush = false;
	m_LegacyMainConfigExecuted = false;
	m_LegacyMapConfigsExecuted = false,
	m_legacyMapConfigNextTime = 0.0f;
}

void CoreConfig::ExecuteMainConfig()
{
	if (m_LegacyMainConfigExecuted)
	{
		return;
	}

	char path[PLATFORM_MAX_PATH];
	char command[PLATFORM_MAX_PATH + sizeof(CommandFormat)];

	ke::SafeSprintf(path, sizeof(path), "%s/%s/%s", g_mod_name.c_str(), get_localinfo("amxx_configsdir", "addons/amxmodx/configs"), MainConfigFile);
	ke::SafeSprintf(command, sizeof(command), CommandFormat, path);

	SERVER_COMMAND(command);
}

void CoreConfig::ExecuteAutoConfigs()
{
	for (size_t i = 0; i < static_cast<size_t>(g_plugins.getPluginsNum()); ++i)
	{
		auto plugin = g_plugins.findPlugin(i);

		bool can_create = true;

		for (size_t j = 0; j < plugin->GetConfigCount(); ++j)
		{
			can_create = ExecuteAutoConfig(plugin, plugin->GetConfig(j), can_create);
		}
	}

	executeForwards(m_ConfigsBufferedForward);
}

bool CoreConfig::ExecuteAutoConfig(CPluginMngr::CPlugin *plugin, AutoConfig *config, bool can_create)
{
	bool will_create = false;

	const char *configsDir = get_localinfo("amxx_configsdir", "addons/amxmodx/configs");

	if (can_create && config->create)
	{
		will_create = true;

		const char *folder = config->folder.c_str();

		char path[PLATFORM_MAX_PATH];
		char build[PLATFORM_MAX_PATH];

		build_pathname_r(path, sizeof(path), "%s%s/%s", configsDir, AutoConfigDir, folder);

		if (!g_LibSys.IsPathDirectory(path))
		{
			char *cur_ptr = path;

			g_LibSys.PathFormat(path, sizeof(path), "%s", folder);
			build_pathname_r(build, sizeof(build), "%s%s", configsDir, AutoConfigDir);

			size_t length = strlen(build);

			do
			{
				char *next_ptr = cur_ptr;

				while (*next_ptr != '\0')
				{
					if (*next_ptr == PLATFORM_SEP_CHAR)
					{
						*next_ptr = '\0';
						next_ptr++;
						break;
					}

					next_ptr++;
				}

				if (*next_ptr == '\0')
				{
					next_ptr = nullptr;
				}

				length += g_LibSys.PathFormat(&build[length], sizeof(build) - length, "/%s", cur_ptr);

				if (!g_LibSys.CreateFolder(build))
				{
					break;
				}

				cur_ptr = next_ptr;

			} while (cur_ptr);
		}
	}

	char file[PLATFORM_MAX_PATH];

	if (config->folder.size())
	{
		ke::SafeSprintf(file, sizeof(file), "%s/%s%s/%s/%s.cfg", g_mod_name.c_str(), configsDir, AutoConfigDir, config->folder.c_str(), config->autocfg.c_str());
	}
	else
	{
		ke::SafeSprintf(file, sizeof(file), "%s/%s%s/%s.cfg", g_mod_name.c_str(), configsDir, AutoConfigDir, config->autocfg.c_str());
	}

	bool file_exists = g_LibSys.IsPathFile(file);

	if (!file_exists && will_create)
	{
		auto list = g_CvarManager.GetCvarsList();

		if (list->empty())
		{
			return can_create;
		}

		FILE *fp = fopen(file, "wt");

		if (fp)
		{
			fprintf(fp, "// This file was auto-generated by AMX Mod X (v%s)\n", AMXX_VERSION);

			if (*plugin->getTitle() && *plugin->getAuthor() && *plugin->getVersion())
			{
				fprintf(fp, "// Cvars for plugin \"%s\" by \"%s\" (%s, v%s)\n", plugin->getTitle(), plugin->getAuthor(), plugin->getName(), plugin->getVersion());	
			}
			else
			{
				fprintf(fp, "// Cvars for plugin \"%s\"\n", plugin->getName());
			}

			fprintf(fp, "\n\n");

			for (auto iter = list->begin(); iter != list->end(); iter++)
			{
				auto info = (*iter);

				if (info->pluginId == plugin->getId())
				{
					char description[255];
					char *ptr = description;

					// Print comments until there is no more
					strncopy(description, info->description.c_str(), sizeof(description));

					while (*ptr != '\0')
					{
						// Find the next line
						char *next_ptr = ptr;

						while (*next_ptr != '\0')
						{
							if (*next_ptr == '\n')
							{
								*next_ptr = '\0';
								next_ptr++;
								break;
							}

							next_ptr++;
						}

						fprintf(fp, "// %s\n", ptr);

						ptr = next_ptr;
					}

					fprintf(fp, "// -\n");
					fprintf(fp, "// Default: \"%s\"\n", info->defaultval.c_str());

					if (info->bound.hasMin)
					{
						fprintf(fp, "// Minimum: \"%02f\"\n", info->bound.minVal);
					}

					if (info->bound.hasMax)
					{
						fprintf(fp, "// Maximum: \"%02f\"\n", info->bound.maxVal);
					}

					fprintf(fp, "%s \"%s\"\n", info->var->name, info->defaultval.c_str());
					fprintf(fp, "\n");
				}
			}

			fprintf(fp, "\n");

			file_exists = true;
			can_create = false;

			fclose(fp);
		}
		else
		{
			AMXXLOG_Error("Failed to auto generate config for %s, make sure the directory has write permission.", plugin->getName());
			return can_create;
		}
	}

	if (file_exists)
	{
		char command[PLATFORM_MAX_PATH + sizeof(CommandFormat)];
		ke::SafeSprintf(command, sizeof(command), CommandFormat, file);

		SERVER_COMMAND(command);
	}

	return can_create;
}

void CoreConfig::ExecuteMapConfig()
{
	const char *configsDir = get_localinfo("amxx_configsdir", "addons/amxmodx/configs");

	char cfgPath[PLATFORM_MAX_PATH];
	char mapName[PLATFORM_MAX_PATH];
	char command[PLATFORM_MAX_PATH + sizeof(CommandFormat)];

	strncopy(mapName, STRING(gpGlobals->mapname), sizeof(mapName));

	char *mapPrefix;

	if ((mapPrefix = strtok(mapName, "_")))
	{
		ke::SafeSprintf(cfgPath, sizeof(cfgPath), "%s/%s%s/prefix_%s.cfg", g_mod_name.c_str(), configsDir, MapConfigDir, mapPrefix);

		if (g_LibSys.IsPathFile(cfgPath))
		{
			ke::SafeSprintf(command, sizeof(command), CommandFormat, cfgPath);
			SERVER_COMMAND(command);
		}
	}

	strncopy(mapName, STRING(gpGlobals->mapname), sizeof(mapName));
	ke::SafeSprintf(cfgPath, sizeof(cfgPath), "%s/%s%s/%s.cfg", g_mod_name.c_str(), configsDir, MapConfigDir, mapName);

	if (g_LibSys.IsPathFile(cfgPath))
	{
		ke::SafeSprintf(command, sizeof(command), CommandFormat, cfgPath);
		SERVER_COMMAND(command);
	}

	// Consider all configs be executed to the next frame.
	m_PendingForwardPush = true;
}


void CoreConfig::OnMapConfigTimer()
{
	if (m_ConfigsExecuted)
	{
		return;
	}

	if (m_PendingForwardPush)
	{
		m_PendingForwardPush = false;
		m_ConfigsExecuted = true;

		executeForwards(m_ConfigsExecutedForward);
	}
	else if (!m_LegacyMapConfigsExecuted && m_legacyMapConfigNextTime <= gpGlobals->time)
	{
		ExecuteMapConfig();
	}
}

void CoreConfig::CheckLegacyBufferedCommand(char *command)
{
	if (m_ConfigsExecuted)
	{
		return;
	}

	if (!m_LegacyMainConfigExecuted && strstr(command, MainConfigFile))
	{
		m_LegacyMainConfigExecuted = true;
	}

	if (!m_LegacyMapConfigsExecuted && strstr(command, MapConfigDir))
	{
		m_LegacyMapConfigsExecuted = true;
	}
}

void CoreConfig::SetMapConfigTimer(float time)
{
	m_legacyMapConfigNextTime = gpGlobals->time + time;
}
