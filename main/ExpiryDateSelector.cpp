#include "ExpiryDateSelector.h"
#include "esp_log.h"
#include <cstdlib>

static const char *TAG = "ExpiryDateSelector";

void ExpiryDateSelector::show()
{
    char **labels = new char *[10];
    updateLabels(labels);

    LVGLManager::updateExpiryMatrixButton(labels);
    LVGLManager::showExpiryMatrix(handleSelection, this);
}

void ExpiryDateSelector::handleSelection(int selectedIndex, void *user_data)
{
    auto *self = static_cast<ExpiryDateSelector *>(user_data);
    ESP_LOGI(TAG, "User selected index: %d", selectedIndex);

    if (selectedIndex == 5)
    { // "<<"
        if (self->dayOffset > 0)
            self->dayOffset -= 5;
        self->show();
    }
    else if (selectedIndex == 6)
    { // ">>"
        self->dayOffset += 5;
        self->show();
    }
    else if (selectedIndex == 7)
    { // "X" skip
        self->service->manageUpdateProduct(self->product);
    }
    else if (selectedIndex >= 0 && selectedIndex < 5)
    {
        time_t now = time(nullptr);
        tm date_tm{};
        localtime_r(&now, &date_tm);

        int dayIndex = selectedIndex;
        date_tm.tm_mday += self->dayOffset + dayIndex + 1;
        mktime(&date_tm);

        char expiryDate[16];
        strftime(expiryDate, sizeof(expiryDate), "%Y-%m-%d", &date_tm);
        self->product.expiry = expiryDate;
        ESP_LOGI(TAG, "Selected expiry date: %s", self->product.expiry.c_str());

        self->service->manageUpdateProduct(self->product);
    }
    else
    {
        LVGLManager::showErrorSnackbar("Invalid selection");
        self->show();
    }
}

void ExpiryDateSelector::updateLabels(char **labels)
{
    time_t now = time(nullptr);

    for (int i = 0; i < 9; ++i)
    {
        if (i == 4)
        {
            labels[i] = strdup("\n");
        }
        else if (i == 6)
        {
            labels[i] = strdup("<<");
        }
        else if (i == 7)
        {
            labels[i] = strdup(">>");
        }
        else if (i == 8)
        {
            labels[i] = strdup("X");
        }
        else
        {
            tm date_tm{};
            localtime_r(&now, &date_tm);

            int dayIndex = (i < 4) ? i : (i - 1);
            date_tm.tm_mday += dayOffset + dayIndex + 1;
            mktime(&date_tm);

            char buf[16];
            strftime(buf, sizeof(buf), "%d/%m", &date_tm);
            labels[i] = strdup(buf);
        }

        if (!labels[i])
            labels[i] = strdup("?");
    }

    labels[9] = nullptr; // terminator
}