#include <iostream>

#include <fmt/format.h>
#include <lyra/lyra.hpp>

#include "config/config.hpp"
#include "serve.hpp"

struct ServeProgram
{
    bool showHelp = false;
    std::string configPath;
    std::string nodeID;

    void addCommand(lyra::group& g)
    {
        g.add_argument(lyra::command("serve", [this](const lyra::group& f) { run(f); })
                           .add_argument(lyra::help(showHelp))
                           .add_argument(lyra::opt(configPath, "config")
                                             .name("--config")
                                             .name("-c")
                                             .required()
                                             .help("Path to the configuration file"))
                           .add_argument(lyra::opt(nodeID, "nodeID")
                                             .name("--nodeID")
                                             .name("-n")
                                             .required()
                                             .help("ID of the node")));
    }

    void run(const lyra::group& g)
    {
        if (showHelp)
        {
            std::cout << g;
            return;
        }

        raft_cli::serve(configPath, nodeID);
    }
};

int main(int argc, char** argv)
{
    bool showHelp = false;
    lyra::group global;
    global.add_argument(lyra::help(showHelp));

    lyra::group subcommands;
    subcommands.require(1, 1);

    ServeProgram serve;
    serve.addCommand(subcommands);

    auto cli = lyra::cli().add_argument(global).add_argument(subcommands);
    auto result = cli.parse({argc, argv});
    if (!result)
    {
        std::cerr << result.message() << '\n';
        return 1;
    }
    if (showHelp)
    {
        std::cout << cli;
        return 0;
    }

    return 0;
}