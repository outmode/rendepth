#include "NativeDisplaySelection.h"
#include <cstdio>
#include <initializer_list>

// Verify supported display models are selected once regardless of enumeration order or duplicates.
int main() {
    int failures = 0;
    // Both enumeration orders, including duplicates before the other model.
    for (bool first : {false, true}) {
        NativeDisplaySelection selection;
        if (!selection.add(first)) ++failures;
        if (selection.add(first)) ++failures;
        if (!selection.add(!first)) ++failures;
        if (selection.add(first) || selection.add(!first)) ++failures;
    }
    std::printf("Native display selection: %d failures\n", failures);
    return failures ? 1 : 0;
}
