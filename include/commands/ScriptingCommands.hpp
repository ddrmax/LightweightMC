#pragma once

#include "core/CommandManager.hpp"

// --- Execution & scripting commands: /execute, /function, /return, /schedule, /trigger.


class ExecuteCommand : public Command
{
public:
    std::string getName() const override { return "execute"; }
    std::string getDescription() const override { return "Executes another command under certain conditions."; }
    std::string getUsage() const override { return "/execute <subcommand>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Executing the subcommand...");
    }
};
class FunctionCommand : public Command
{
public:
    std::string getName() const override { return "function"; }
    std::string getDescription() const override { return "Executes a datapack function."; }
    std::string getUsage() const override { return "/function <name>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Function executed.");
    }
};
class ReturnCommand : public Command
{
public:
    std::string getName() const override { return "return"; }
    std::string getDescription() const override { return "Controls the flow of execution in functions."; }
    std::string getUsage() const override { return "/return <value|fail|run>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Specified return value.");
    }
};
class ScheduleCommand : public Command
{
public:
    std::string getName() const override { return "schedule"; }
    std::string getDescription() const override { return "Delays the execution of a function."; }
    std::string getUsage() const override { return "/schedule <function> <time>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.size() < 2)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Planned function.");
    }
};
class TriggerCommand : public Command
{
public:
    std::string getName() const override { return "trigger"; }
    std::string getDescription() const override { return "Sets a trigger to be activated by a non-op player."; }
    std::string getUsage() const override { return "/trigger <objective> [add|set] [value]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 0; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Trigger activated.");
    }
};
