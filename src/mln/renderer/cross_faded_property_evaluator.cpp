#include <mln/renderer/cross_faded_property_evaluator.hpp>
#include <mln/style/expression/image.hpp>
#include <mln/util/chrono.hpp>

#include <cmath>

namespace mln {

template <typename T>
Faded<T> CrossFadedPropertyEvaluator<T>::operator()(const style::Undefined&) const {
    return calculate(defaultValue, defaultValue, defaultValue);
}

template <typename T>
Faded<T> CrossFadedPropertyEvaluator<T>::operator()(const T& constant) const {
    return calculate(constant, constant, constant);
}

template <typename T>
Faded<T> CrossFadedPropertyEvaluator<T>::operator()(const style::PropertyExpression<T>& expression) const {
    // the values at the whole zoom below and the next one (getCrossfadeParameters)
    const float below = std::floor(parameters.z);
    return calculate(expression.evaluate(below), expression.evaluate(below), expression.evaluate(below + 1.0f));
}

template <typename T>
Faded<T> CrossFadedPropertyEvaluator<T>::calculate(const T& min, const T& mid, const T& max) const {
    // from the value at the whole zoom below to the one at the next (PropertyEvaluationParameters::getCrossfadeParameters)
    (void)mid;
    return Faded<T>{min, max};
}

template class CrossFadedPropertyEvaluator<style::expression::Image>;
template class CrossFadedPropertyEvaluator<std::vector<float>>;

} // namespace mln
