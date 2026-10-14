#include "TorrentHandler.h"

#include <iostream>

int main(int argc, char* argv[])
{

    if (argc < 2 or argc > 3)
    {
        std::cerr << "Usage: " << argv[0] << " <filename> (optional)<output dir name>\n";
        return 0;
    }
    std::string filename = argv[1];

    TorrentSettings settings;
    settings.peerID          = "-ME0001-q]zusmisihrs";
    settings.port            = 6885;
    settings.compact         = 1;
    settings.numwant         = 30;
    settings.peerClientCount = 10;

    if (argc > 2)
    {
        settings.outputDirName = argv[2];
    }

    TorrentHandler handler(settings);

    if (const auto& res = handler.loadTorrentAndPeers(filename); !res)
    {
        std::cout << res.error();

        return 0;
    }

    if (const auto& res = handler.downloadFile(); !res)
    {
        std::cout << res.error();

        return 0;
    }

    return 0;
}
