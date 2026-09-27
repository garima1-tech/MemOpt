#ifndef TIER_MANAGER_H
#define TIER_MANAGER_H

#include <string>
#include <unordered_map>

class TierManager
{
private:

    std::unordered_map<
        std::string,
        std::string
    > pages;

public:

    void placePage(
        const std::string& page,
        const std::string& tier
    );

    std::string getTier(
        const std::string& page
    );

    void showPages();
};

#endif