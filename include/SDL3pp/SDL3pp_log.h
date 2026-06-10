#ifndef SDL3PP_LOG_UMBRELLA_H_
#define SDL3PP_LOG_UMBRELLA_H_

/**
 * @file SDL3pp_log.h
 * @brief Umbrella header for the SDL3pp logging module.
 *
 * This module provides a flexible logging system with multiple sinks (console,
 * file, etc.) and log levels. The `Logger` class manages log messages and
 * dispatches them to registered sinks. It also includes an optional bridge to
 * redirect SDL's built-in logging functions to the SDL3pp logger.
 *
 * ## Usage
 *
 * ```cpp
 * SDL::Log("Hello, {}!", "world");
 *
 * // Create a logger with console and file sinks
 * SDL::Logger logger;
 * logger.AddSink(std::make_unique<SDL::ConsoleSink>());
 * logger.AddSink(std::make_unique<SDL::FileSink>("log.txt"));
 *
 * logger.Log(SDL_LOG_PRIORITY_INFO, "This is an info message.");
 * logger.Log(SDL_LOG_PRIORITY_ERROR, "This is an error message.");
 * ```
 */
#include "SDL3pp_log/Logger.h"

#endif /* SDL3PP_LOG_UMBRELLA_H_ */
