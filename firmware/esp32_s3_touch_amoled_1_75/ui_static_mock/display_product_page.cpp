#include "display_product_page.h"

#include <atomic>

namespace {

std::atomic<uint8_t> currentPage {
    static_cast<uint8_t>(DisplayProductPage::Other)
};

} // namespace

void displayProductPageSet(DisplayProductPage page)
{
    currentPage.store(static_cast<uint8_t>(page), std::memory_order_release);
}

DisplayProductPage displayProductPageGet()
{
    return static_cast<DisplayProductPage>(
        currentPage.load(std::memory_order_acquire)
    );
}
