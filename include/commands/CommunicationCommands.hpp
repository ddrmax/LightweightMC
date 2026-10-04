#pragma once

#include "core/CommandManager.hpp"


// // ==========================================


class MeCommand : public Command
{
public:
    std::string getName() const override { return "me"; }
    std::string getDescription() const override { return "Displays an action message about the sender."; }
    std::string getUsage() const override { return "/me <action>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 0; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        std::string action;
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (i > 0)
                action += " ";
            action += args[i];
        }
        ctx.reply("* " + ctx.sender + " " + action);
    }
};


class ParticleCommand : public Command
{
public:
    std::string getName() const override { return "particle"; }
    std::string getDescription() const override { return "Creates visual particles."; }
    std::string getUsage() const override { return "/particle <name> [pos]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Particles created.");
    }
};


class PlaysoundCommand : public Command
{
public:
    std::string getName() const override { return "playsound"; }
    std::string getDescription() const override { return "Plays a sound."; }
    std::string getUsage() const override { return "/playsound <sound> <source> <targets>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Sound Played.");
    }
};


class StopsoundCommand : public Command
{
public:
    std::string getName() const override { return "stopsound"; }
    std::string getDescription() const override { return "Stops a sound."; }
    std::string getUsage() const override { return "/stopsound <targets> [source] [sound]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Sound stopped");
    }
};


class TeammsgCommand : public Command
{
public:
    std::string getName() const override { return "teammsg"; }
    std::string getDescription() const override { return "Send a message to your team."; }
    std::string getUsage() const override { return "/teammsg <message>"; }
    std::vector<std::string> getAliases() const override { return {"tm"}; }
    int getRequiredOpLevel() const override { return 0; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Team message sent.");
    }
};


class TellrawCommand : public Command
{
public:
    std::string getName() const override { return "tellraw"; }
    std::string getDescription() const override { return "Displays a raw JSON message to players."; }
    std::string getUsage() const override { return "/tellraw <targets> <json>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.size() < 2)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Tellraw message sent.");
    }
};


class TitleCommand : public Command
{
public:
    std::string getName() const override { return "title"; }
    std::string getDescription() const override { return "Manages the titles displayed on the screen."; }
    std::string getUsage() const override { return "/title <target> <title|subtitle|actionbar|clear|reset> ..."; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.size() < 2)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Title updated.");
    }
};
