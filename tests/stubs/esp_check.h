#pragma once
#define ESP_RETURN_ON_ERROR(expr, tag, ...) do { (void)(tag); int error_ = (expr); if (error_) return error_; } while (0)
#define ESP_RETURN_ON_FALSE(expr, error, tag, ...) do { (void)(tag); if (!(expr)) return error; } while (0)
