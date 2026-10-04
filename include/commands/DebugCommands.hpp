#pragma once

#include "core/CommandManager.hpp"


// // ==========================================


class DebugCommand : public Command
{
public:
    std::string getName() const override { return "debug"; }
    std::string getDescription() const override { return "Start or stop a debugging session."; }
    std::string getUsage() const override { return "/debug <start|stop>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 3; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Debug Session" + args[0]);
    }
};


class JfrCommand : public Command
{
public:
    std::string getName() const override { return "jfr"; }
    std::string getDescription() const override { return "Starts or stops JFR profiling."; }
    std::string getUsage() const override { return "/jfr. <start|stop>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 4; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("JFR profiling" + args[0]);
    }
};


class PerfCommand : public Command
{
public:
    std::string getName() const override { return "perf"; }
    std::string getDescription() const override { return "Captures metrics for 10 seconds."; }
    std::string getUsage() const override { return "/perf <start|stop>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 4; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        ctx.reply("Performance sampling runs for 10s...");
    }
};
