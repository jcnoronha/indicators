#pragma once

#include <indicators/color.hpp>
#include <indicators/details/stream_helper.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <indicators/setting.hpp>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>

namespace indicators {

class BlockProgressBar {
  using Settings = std::tuple<option::ForegroundColor, option::BarWidth, option::Start, option::End,
                              option::PrefixText, option::PostfixText, option::ShowPercentage,
                              option::ShowElapsedTime, option::ShowRemainingTime, option::Completed,
                              option::SavedStartTime, option::MaxPostfixTextLen, option::FontStyles,
                              option::MaxProgress, option::Stream>;

public:
  template <typename... Args,
            typename std::enable_if<details::are_settings_from_tuple<
                                        Settings, typename std::decay<Args>::type...>::value,
                                    void *>::type = nullptr>
  explicit BlockProgressBar(Args &&... args)
      : settings_(details::get<details::ProgressBarOption::foreground_color>(
                      option::ForegroundColor{Color::unspecified}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::bar_width>(option::BarWidth{100},
                                                                      std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::start>(option::Start{"["},
                                                                  std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::end>(option::End{"]"},
                                                                std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::prefix_text>(
                      option::PrefixText{""}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::postfix_text>(
                      option::PostfixText{""}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::show_percentage>(
                      option::ShowPercentage{true}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::show_elapsed_time>(
                      option::ShowElapsedTime{false}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::show_remaining_time>(
                      option::ShowRemainingTime{false}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::completed>(option::Completed{false},
                                                                      std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::saved_start_time>(
                      option::SavedStartTime{false}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::max_postfix_text_len>(
                      option::MaxPostfixTextLen{0}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::font_styles>(
                      option::FontStyles{std::vector<FontStyle>{}}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::max_progress>(
                      option::MaxProgress{100}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::stream>(option::Stream{std::cout},
                                                                   std::forward<Args>(args)...)) {}

  template <typename T, details::ProgressBarOption id>
  void set_option(details::Setting<T, id> &&setting) {
    static_assert(!std::is_same<T, typename std::decay<decltype(details::get_value<id>(
                                       std::declval<Settings>()))>::type>::value,
                  "Setting has wrong type!");
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<id>() = std::move(setting).value;
  }

  template <typename T, details::ProgressBarOption id>
  void set_option(const details::Setting<T, id> &setting) {
    static_assert(!std::is_same<T, typename std::decay<decltype(details::get_value<id>(
                                       std::declval<Settings>()))>::type>::value,
                  "Setting has wrong type!");
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<id>() = setting.value;
  }

  void set_option(
      const details::Setting<std::string, details::ProgressBarOption::postfix_text> &setting) {
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<details::ProgressBarOption::postfix_text>() = setting.value;
    if (setting.value.length() > get_value<details::ProgressBarOption::max_postfix_text_len>()) {
      get_value<details::ProgressBarOption::max_postfix_text_len>() = setting.value.length();
    }
  }

  void
  set_option(details::Setting<std::string, details::ProgressBarOption::postfix_text> &&setting) {
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<details::ProgressBarOption::postfix_text>() = std::move(setting).value;
    auto &new_value = get_value<details::ProgressBarOption::postfix_text>();
    if (new_value.length() > get_value<details::ProgressBarOption::max_postfix_text_len>()) {
      get_value<details::ProgressBarOption::max_postfix_text_len>() = new_value.length();
    }
  }

  void set_progress(float value) {
    {
      std::lock_guard<std::mutex> lock{mutex_};
      progress_ = value;
    }
    save_start_time();
    print_progress();
  }

  void tick() {
    {
      std::lock_guard<std::mutex> lock{mutex_};
      progress_ += 1;
    }
    save_start_time();
    print_progress();
  }

  size_t current() {
    std::lock_guard<std::mutex> lock{mutex_};
    return std::min(static_cast<size_t>(progress_),
                    size_t(get_value<details::ProgressBarOption::max_progress>()));
  }

  bool is_completed() const { return get_value<details::ProgressBarOption::completed>(); }

  void mark_as_completed() {
    get_value<details::ProgressBarOption::completed>() = true;
    print_progress();
  }

private:
  template <details::ProgressBarOption id>
  auto get_value() -> decltype((details::get_value<id>(std::declval<Settings &>()).value)) {
    return details::get_value<id>(settings_).value;
  }

  template <details::ProgressBarOption id>
  auto get_value() const
      -> decltype((details::get_value<id>(std::declval<const Settings &>()).value)) {
    return details::get_value<id>(settings_).value;
  }

  Settings settings_;
  float progress_{0.0};
  std::chrono::time_point<std::chrono::high_resolution_clock> start_time_point_;
  std::mutex mutex_;

  template <typename Indicator, size_t count> friend class MultiProgress;
  template <typename Indicator> friend class DynamicProgress;
  std::atomic<bool> multi_progress_mode_{false};

  void save_start_time() {
    auto &show_elapsed_time = get_value<details::ProgressBarOption::show_elapsed_time>();
    auto &saved_start_time = get_value<details::ProgressBarOption::saved_start_time>();
    auto &show_remaining_time = get_value<details::ProgressBarOption::show_remaining_time>();
    if ((show_elapsed_time || show_remaining_time) && !saved_start_time) {
      start_time_point_ = std::chrono::high_resolution_clock::now();
      saved_start_time = true;
    }
  }

public:
  void print_progress(bool from_multi_progress = false) {
    std::lock_guard<std::mutex> lock{mutex_};

    auto &os = get_value<details::ProgressBarOption::stream>();

    const auto max_progress = get_value<details::ProgressBarOption::max_progress>();
    if (multi_progress_mode_ && !from_multi_progress) {
      if (progress_ > max_progress) {
        get_value<details::ProgressBarOption::completed>() = true;
      }
      return;
    }

    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start_time_point_);

    if (get_value<details::ProgressBarOption::foreground_color>() != Color::unspecified)
      details::set_stream_color(os, get_value<details::ProgressBarOption::foreground_color>());

    for (auto &style : get_value<details::ProgressBarOption::font_styles>())
      details::set_font_style(os, style);

    os << get_value<details::ProgressBarOption::prefix_text>();
    os << get_value<details::ProgressBarOption::start>();

    details::BlockProgressScaleWriter writer{os,
                                             get_value<details::ProgressBarOption::bar_width>()};
    writer.write(progress_ / max_progress * 100);

    os << get_value<details::ProgressBarOption::end>();
    if (get_value<details::ProgressBarOption::show_percentage>()) {
      os << " " << std::min(static_cast<size_t>(progress_ / max_progress * 100.0), size_t(100))
         << "%";
    }

    auto &saved_start_time = get_value<details::ProgressBarOption::saved_start_time>();

    if (get_value<details::ProgressBarOption::show_elapsed_time>()) {
      os << " [";
      if (saved_start_time)
        details::write_duration(os, elapsed);
      else
        os << "00:00s";
    }

    if (get_value<details::ProgressBarOption::show_remaining_time>()) {
      if (get_value<details::ProgressBarOption::show_elapsed_time>())
        os << "<";
      else
        os << " [";

      if (saved_start_time) {
        auto eta = std::chrono::nanoseconds(
            progress_ > 0 ? static_cast<long long>(elapsed.count() * max_progress / progress_) : 0);
        auto remaining = eta > elapsed ? (eta - elapsed) : (elapsed - eta);
        details::write_duration(os, remaining);
      } else {
        os << "00:00s";
      }

      os << "]";
    } else {
      if (get_value<details::ProgressBarOption::show_elapsed_time>())
        os << "]";
    }

    if (get_value<details::ProgressBarOption::max_postfix_text_len>() == 0)
      get_value<details::ProgressBarOption::max_postfix_text_len>() = 10;
    os << " " << get_value<details::ProgressBarOption::postfix_text>()
       << std::string(get_value<details::ProgressBarOption::max_postfix_text_len>(), ' ') << "\r";
    os.flush();
    if (progress_ > max_progress) {
      get_value<details::ProgressBarOption::completed>() = true;
    }
    if (get_value<details::ProgressBarOption::completed>() &&
        !from_multi_progress) // Don't std::endl if calling from MultiProgress
      os << termcolor::reset << std::endl;
  }
};

} // namespace indicators
