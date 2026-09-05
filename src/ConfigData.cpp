#include "ConfigData.h"

namespace Config
{
    void TargetSet::NormalizePath()
    {
        ModelSet normalized;
        normalized.reserve(models.size());
        for (const auto& model : models) {
            auto path = DSC::NormalizePath(model);
            if (!path.empty()) {
                normalized.insert(std::move(path));
            }
        }
        models = std::move(normalized);
    }
}
