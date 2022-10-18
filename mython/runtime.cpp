#include "runtime.h"

#include <cassert>
#include <optional>
#include <sstream>
#include <algorithm>

using namespace std;

namespace runtime {

ObjectHolder::ObjectHolder(std::shared_ptr<Object> data)
    : data_(std::move(data)) {
}

void ObjectHolder::AssertIsValid() const {
    assert(data_ != nullptr);
}

ObjectHolder ObjectHolder::Share(Object& object) {
    // Возвращаем невладеющий shared_ptr (его deleter ничего не делает)
    return ObjectHolder(std::shared_ptr<Object>(&object, [](auto* /*p*/) { /* do nothing */ }));
}

ObjectHolder ObjectHolder::None() {
    return ObjectHolder();
}

Object& ObjectHolder::operator*() const {
    AssertIsValid();
    return *Get();
}

Object* ObjectHolder::operator->() const {
    AssertIsValid();
    return Get();
}

Object* ObjectHolder::Get() const {
    return data_.get();
}

ObjectHolder::operator bool() const {
    return Get() != nullptr;
}

bool IsTrue(const ObjectHolder& object) {
    if (Number* object_ = object.TryAs<Number>()) {
        return object_->GetValue() != 0;
    }
    if (Bool* object_ = object.TryAs<Bool>()) {
        return object_->GetValue() == true;
    }
    if (String* object_ = object.TryAs<String>()) {
        return !(object_->GetValue().empty());
    }
    return false;
}

void ClassInstance::Print(std::ostream& os, Context& context) {
    if (HasMethod("__str__"s, 0)) {
        Call("__str__"s, {}, context)->Print(os, context);
    } else {
        os << this;
    }
}

bool ClassInstance::HasMethod(const std::string& method, size_t argument_count) const {
    const Method* method_ = cls_.GetMethod(method);
    if (method_ && (method_->formal_params.size() == argument_count)) {
        return true;
    }
    return false;
}

Closure& ClassInstance::Fields() {
    return closure_;
}

const Closure& ClassInstance::Fields() const {
    return closure_;
}

ClassInstance::ClassInstance(const Class& cls)
    : cls_(cls) {
}

ObjectHolder ClassInstance::Call(const std::string& method,
                                 const std::vector<ObjectHolder>& actual_args,
                                 Context& context) {

    if (!HasMethod(method, actual_args.size())) {
        throw std::runtime_error("Error calling method "s + method);
    }

    const Method* method_ = cls_.GetMethod(method);
    
    Closure closure;
    
    closure["self"s] = ObjectHolder::Share(*this);

    size_t index = 0;
    for (auto &param : method_->formal_params) {
        closure[param] = actual_args.at(index++);
    }

    return method_->body->Execute(closure, context);
}

Class::Class(std::string name, std::vector<Method> methods, const Class* parent)
    : name_(std::move(name)), methods_(std::move(methods)), parent_(parent) {
}

const Method* Class::GetMethod(const std::string& name) const {
    
    auto it = std::find_if(methods_.begin(), methods_.end(), 
        [&name](const Method &method) { return method.name == name; });

    if (it != methods_.end()) {
        return &(*it);
    } else if (parent_) {
        return parent_->GetMethod(name);
    }
    return nullptr;
}

// [[nodiscard]] inline const std::string& Class::GetName() const {
[[nodiscard]] const std::string& Class::GetName() const {
    return name_;
}

void Class::Print(ostream& os, [[maybe_unused]] Context& context) {
    os << "Class "s << name_;
}

void Bool::Print(std::ostream& os, [[maybe_unused]] Context& context) {
    os << (GetValue() ? "True"sv : "False"sv);
}

// Функция Equal возвращает true, если её аргументы содержат одинаковые числа, строки или логические значения,
// и false — если разные. Если первый аргумент — экземпляр пользовательского класса с методом __eq__,
// функция возвращает результат вызова lhs.__eq__(rhs), приведённый к типу Bool.
// Если оба аргумента имеют значение None, функция возвращает true.
// В остальных случаях выбрасывается исключение runtime_error.
bool Equal(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {

    Number* number_l = lhs.TryAs<Number>();
    Number* number_r = rhs.TryAs<Number>();
    if (number_l && number_r) {
        return number_l->GetValue() == number_r->GetValue();
    }

    String* string_l = lhs.TryAs<String>();
    String* string_r = rhs.TryAs<String>();
    if (string_l && string_r) {
        return string_l->GetValue() == string_r->GetValue();
    }

    Bool* bool_l = lhs.TryAs<Bool>();
    Bool* bool_r = rhs.TryAs<Bool>();
    if (bool_l && bool_r) {
        return bool_l->GetValue() == bool_r->GetValue();
    }

    if (ClassInstance* instance = lhs.TryAs<ClassInstance>()) {
        return IsTrue(instance->Call("__eq__"s, {rhs}, context));
    }

    if (!lhs && !rhs) {
        return true;
    }
    throw std::runtime_error("Cannot compare objects for equality"s);
}

// Функция Less для объектов, которые хранят строки, числа и логические значения, возвращает результат сравнения,
// используя оператор <. Если первый аргумент — объект пользовательского класса с методом __lt__,
// функция возвращает результат вызова lhs.__lt__(rhs), приведённый к типу Bool.
// В остальных случаях выбрасывается исключение runtime_error.
bool Less(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {

    Number* number_l = lhs.TryAs<Number>();
    Number* number_r = rhs.TryAs<Number>();
    if (number_l && number_r) {
        return number_l->GetValue() < number_r->GetValue();
    }

    String* string_l = lhs.TryAs<String>();
    String* string_r = rhs.TryAs<String>();
    if (string_l && string_r) {
        return string_l->GetValue() < string_r->GetValue();
    }

    Bool* bool_l = lhs.TryAs<Bool>();
    Bool* bool_r = rhs.TryAs<Bool>();
    if (bool_l && bool_r) {
        return bool_l->GetValue() < bool_r->GetValue();
    }

    if (ClassInstance* instance = lhs.TryAs<ClassInstance>()) {
        return IsTrue(instance->Call("__lt__"s, {rhs}, context));
    }

    throw std::runtime_error("Cannot compare objects for less"s);
}

bool NotEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return !Equal(lhs, rhs, context);
}

bool Greater(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return !(Less(lhs, rhs, context) || Equal(lhs, rhs, context));
}

bool LessOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return !Greater(lhs, rhs, context);
}

bool GreaterOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return !Less(lhs, rhs, context);
}

}  // namespace runtime
