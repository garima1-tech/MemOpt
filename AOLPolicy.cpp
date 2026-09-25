#include "AOLPolicy.h"

std::string AOLPolicy::decide(
    double aol)
{
    const double threshold = 50.0;

    if (aol >= threshold)
        return "FAST";

    return "SLOW";
}