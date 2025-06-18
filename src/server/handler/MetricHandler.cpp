//
// Created by Emil Ebert on 15.06.25.
//

#include "MetricHandler.h"

std::unordered_map<std::string, size_t> MetricHandler::metrics;
std::unordered_map<std::string, size_t> MetricHandler::lastFullMetrics;
std::time_t MetricHandler::lastResetTime = std::time(nullptr);

void MetricHandler::incrementMetric(const std::string &metricName, size_t value) {
    metrics[metricName] += value;
}

std::unordered_map<std::string, size_t> &MetricHandler::getAllFullMetric() {
    return lastFullMetrics;
}

void MetricHandler::resetMetrics() {
    if (std::time(nullptr) - lastResetTime > RESET_INTERVAL) {
        for (auto metric : metrics)
            lastFullMetrics[metric.first] = metric.second;
        metrics.clear();
        lastResetTime = std::time(nullptr);
    }
}

std::time_t MetricHandler::getLastResetTime() {
    return lastResetTime;
}


