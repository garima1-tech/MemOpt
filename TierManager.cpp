#include "TierManager.h"

#include <iostream>

void TierManager::placePage(
    const std::string& page,
    const std::string& tier)
{
    pages[page] = tier;
}


std::string TierManager::getTier(
    const std::string& page)
{
    if (pages.find(page) == pages.end())
        return "UNKNOWN";

    return pages[page];
}


void TierManager::showPages()
{
    std::cout << "\nMemory Placement\n";
    std::cout << "----------------\n";

    for (const auto& item : pages)
    {
        std::cout
            << item.first
            << " -> "
            << item.second
            << "\n";
    }
}