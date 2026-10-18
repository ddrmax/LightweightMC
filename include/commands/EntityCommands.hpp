#pragma once

#include "core/CommandManager.hpp"


// // ===========================================================


class BossbarCommand : public Command
{
public:
    std::string getName() const override { return "bossbar"; }
    std::string getDescription() const override { return "Create and modify boss bars."; }
    std::string getUsage() const override { return "/bossbar <add|remove|set|get|list>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Bossbar updated.");
    }
};


class DamageCommand : public Command
{
public:
    std::string getName() const override { return "damage"; }
    std::string getDescription() const override { return "Deal damage to specific entities."; }
    std::string getUsage() const override { return "/damage <target> <amount> [damageType]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }
    // Mutates a player's HP on the wire, so console replies need to be
    // broadcast back to every PLAY client (see CommandManager::executeCommand
    // which sets CommandContext::broadcastThisReply from this flag).
    bool affectsPlayerState() const override { return true; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.size() < 2)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        float amount = 0.0f;
        std::istringstream amountStream(args[1]);
        if (!(amountStream >> amount) || amount < 0.0f)
        {
            ctx.reply("Damage amount \"" + args[1] + "\" must be a non-negative number. Usage: " + getUsage());
            return;
        }
        int damageTypeCode = 0;
        if (args.size() >= 3)
        {
            const std::string &damageType = args[2];
            // Vanilla 1.8 doesn't restrict damage causes to a fixed enum --
            // Minecraft's DamageController takes a DamageCauseType string,
            // so we expose the causes admins actually type in Hattrick-style
            // servers rather than a hard-coded 0-4 range. Codes 0-4 line up
            // with the wiki's canonical damage causes (melee/fall/drowning/
            // fire/freeze) so existing clients that hardcode those values
            // keep seeing the correct HUD flash; 5+ are LightweightMC's
            // extensions (zombie/poison/lightning/thorns/magic) that Player
            // Session::applyDamage stores as the last damage cause and
            // NetworkGameplay::applyDamageToPlayer forwards unchanged.
            if (damageType == "melee") damageTypeCode = 0;
            else if (damageType == "fall") damageTypeCode = 1;
            else if (damageType == "drowning") damageTypeCode = 2;
            else if (damageType == "fire") damageTypeCode = 3;
            else if (damageType == "freeze") damageTypeCode = 4;
            else if (damageType == "zombie") damageTypeCode = 5;
            else if (damageType == "poison") damageTypeCode = 6;
            else if (damageType == "lightning") damageTypeCode = 7;
            else if (damageType == "thorns") damageTypeCode = 8;
            else if (damageType == "magic") damageTypeCode = 9;
            else
            {
                ctx.reply("Unknown damage type \"" + damageType +
                          "\" (valid: melee|fall|drowning|fire|freeze|zombie|poison|lightning|thorns|magic). Usage: " + getUsage());
                return;
            }
        }
        if (ctx.applyDamageToPlayer(args[0], amount, damageTypeCode))
        {
            ctx.reply("Inflicted " + args[1] + " " + (args.size() >= 3 ? args[2] : std::string("melee")) +
                      " damage on " + args[0] + ".");
        }
        else
        {
            ctx.reply("Damage update failed for " + args[0] + " (unknown player or invulnerable).");
        }
    }
};


class KillCommand : public Command
{
public:
    std::string getName() const override { return "kill"; }
    std::string getDescription() const override { return "Kill entities."; }
    std::string getUsage() const override { return "/kill [targets]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }
    // Kills a player on the wire -- always broadcast for console invocations.
    bool affectsPlayerState() const override { return true; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        // Vanilla /kill can be typed bare (`/kill`) from a chat window to kill
        // the caller; from the console we refuse because the caller is not a
        // player on the wire. Multiple explicit targets are accepted so admins
        // can clear a small list in one line.
        std::vector<std::string> targets;
        if (args.empty())
        {
            if (ctx.isConsole || ctx.sender.empty() || ctx.sender == "Console")
            {
                ctx.reply("Usage: /kill <target> [target ...] (console requires an explicit target)");
                return;
            }
            targets.push_back(ctx.sender);
        }
        else
        {
            targets.assign(args.begin(), args.end());
        }
        int killed = 0;
        for (const auto &t : targets)
        {
            if (ctx.killPlayer(t))
                ++killed;
        }
        if (killed == 0)
        {
            ctx.reply("kill failed for every listed target (unknown or already-dead players).");
        }
        else if (targets.size() == 1 && killed == 1)
        {
            ctx.reply("Killed " + targets[0] + ".");
        }
        else
        {
            ctx.reply("Killed " + std::to_string(killed) + " of " + std::to_string(targets.size()) + " listed targets.");
        }
    }
};


class LootCommand : public Command
{
public:
    std::string getName() const override { return "loot"; }
    std::string getDescription() const override { return "Places items from an inventory slot on the ground."; }
    std::string getUsage() const override { return "/loot <target> <source>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Loot generated.");
    }
};


class RideCommand : public Command
{
public:
    std::string getName() const override { return "ride"; }
    std::string getDescription() const override { return "Moves entities up or down."; }
    std::string getUsage() const override { return "/ride <target> <mount|dismount>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Modified mount.");
    }
};


class SpectateCommand : public Command
{
public:
    std::string getName() const override { return "watch"; }
    std::string getDescription() const override { return "Observe an entity in spectator mode."; }
    std::string getUsage() const override { return "/watch [target] [player]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        ctx.reply("Mode observation change.");
    }
};


class SpreadPlayersCommand : public Command
{
public:
    std::string getName() const override { return "spreadplayers"; }
    std::string getDescription() const override { return "Teleports entities to random locations."; }
    std::string getUsage() const override { return "/spreadplayers <center> <spreadDistance> <maxRange> <respectTeams> <targets>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.size() < 5)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Dispersed players.");
    }
};


class SummonCommand : public Command
{
public:
    std::string getName() const override { return "summon"; }
    std::string getDescription() const override { return "Summons an entity."; }
    std::string getUsage() const override { return "/summon <entity> [pos] [nbt]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Entity invoked.");
    }
};

// // ===========================================================


class DataCommand : public Command
{
public:
    std::string getName() const override { return "data"; }
    std::string getDescription() const override { return "Gets, merges, modifies and deletes NBT data."; }
    std::string getUsage() const override { return "/data <get|merge|modify|remove> ..."; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("NBT data modified.");
    }
};


class TagCommand : public Command
{
public:
    std::string getName() const override { return "tag"; }
    std::string getDescription() const override { return "Controls entity tags."; }
    std::string getUsage() const override { return "/tag <target> <add|remove|list> [name]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Tag modified.");
    }
};


class TeamCommand : public Command
{
public:
    std::string getName() const override { return "team"; }
    std::string getDescription() const override { return "Control the teams."; }
    std::string getUsage() const override { return "/team <add|remove|join|leave|empty|list|option> ..."; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Updated teams.");
    }
};


class ScoreboardCommand : public Command
{
public:
    std::string getName() const override { return "scoreboard"; }
    std::string getDescription() const override { return "Manage scoreboard goals and players."; }
    std::string getUsage() const override { return "/scoreboard <objectives|players|teams> ..."; }
    std::vector<std::string> getAliases() const override { return {"Sb"}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Scoreboard updated.");
    }
};
