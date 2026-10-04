#pragma once

#include "core/CommandManager.hpp"

// --- System & core game commands (remaining system utilities).

// --- Command /compute (mockup) ---
class ComputeCommand : public Command
{
public:
    std::string getName() const override { return "compute"; }
    std::string getDescription() const override { return "Evaluates an internal number provider directly."; }
    std::string getUsage() const override { return "/compute <provider> ..."; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        // TODO: Implement number provider evaluation.
        ctx.reply("Computation completed.");
    }
};
