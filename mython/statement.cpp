#include "statement.h"

#include <iostream>
#include <sstream>

using namespace std;

namespace ast {

using runtime::Closure;
using runtime::Context;
using runtime::ObjectHolder;

namespace {
const string ADD_METHOD = "__add__"s;
const string INIT_METHOD = "__init__"s;
}  // namespace

VariableValue::VariableValue(const std::string& var_name)
    : dotted_ids_({var_name}) {
}

VariableValue::VariableValue(std::vector<std::string> dotted_ids)
    : dotted_ids_(std::move(dotted_ids)) {
}

ObjectHolder VariableValue::Execute(Closure& closure, Context& context) {
    if (closure.count(dotted_ids_[0])) {
        auto variable = closure.at(dotted_ids_[0]);
        if (dotted_ids_.size() > 1) {
            if (auto object = variable.TryAs<runtime::ClassInstance>()) {
                std::vector<std::string> new_dotted_ids_;
                new_dotted_ids_.insert(new_dotted_ids_.begin(), dotted_ids_.begin() + 1, dotted_ids_.end());
                return VariableValue(std::move(new_dotted_ids_)).Execute(object->Fields(), context);
            } else {
                throw std::runtime_error("Error cast to ClassInstance"s);
            }
        }
        return variable;
    } else {
        throw std::runtime_error("Variable not found"s);
    }
}

Assignment::Assignment(std::string var, std::unique_ptr<Statement> rv)
    : var_(std::move(var)), rv_(std::move(rv)) {
}

ObjectHolder Assignment::Execute(Closure& closure, Context& context) {
    closure[var_] = rv_->Execute(closure, context);
    return closure.at(var_);
}

FieldAssignment::FieldAssignment(VariableValue object, std::string field_name,
                                 std::unique_ptr<Statement> rv)
    : object_(std::move(object)), field_name_(std::move(field_name)), rv_(std::move(rv)) {
}

ObjectHolder FieldAssignment::Execute(Closure& closure, Context& context) {
    auto object = object_.Execute(closure, context).TryAs<runtime::ClassInstance>();
    if (object) {
        return object->Fields()[field_name_] = rv_->Execute(closure, context);
    } else {
        throw runtime_error("Error cast to ClassInstance"s);
    }
}

unique_ptr<Print> Print::Variable(const std::string& name) {
    return std::make_unique<Print>(std::make_unique<VariableValue>(name));
}

Print::Print(unique_ptr<Statement> argument) {
    args_.push_back(std::move(argument));
}

Print::Print(vector<unique_ptr<Statement>> args)
    : args_(std::move(args)) {
}

ObjectHolder Print::Execute(Closure& closure, Context& context) {
    bool isFirst = true;
    auto& out = context.GetOutputStream();

    for (auto& arg : args_) {
        if (!isFirst) {
            out << " "s;
        }
        auto object = arg->Execute(closure, context);
        if (object) {
            object->Print(out, context);
        } else {
            out << "None"s;
        }
        isFirst = false;
    }
    out << "\n"s;
    return {};
}

MethodCall::MethodCall(std::unique_ptr<Statement> object, std::string method,
                       std::vector<std::unique_ptr<Statement>> args)
    : object_(std::move(object)), method_(std::move(method)), args_(std::move(args)) {
}

ObjectHolder MethodCall::Execute(Closure& closure, Context& context) {
    auto object = object_->Execute(closure, context).TryAs<runtime::ClassInstance>();
    if (object) {
        std::vector<runtime::ObjectHolder> actual_args;
        actual_args.reserve(args_.size());

        for (const auto& arg : args_) {
            actual_args.push_back(arg->Execute(closure, context));
        }

        return object->Call(method_, actual_args, context);
    } else {
        throw runtime_error("Error cast to ClassInstance"s);
    }
}

MethodBody::MethodBody(std::unique_ptr<Statement>&& body)
    : body_(std::move(body)) {
}

ObjectHolder MethodBody::Execute(Closure& closure, Context& context) {
    try {
        return body_->Execute(closure, context);
    } catch (ObjectHolder result) {
        return result;
    }
}

ObjectHolder Return::Execute(Closure& closure, Context& context) {
    throw statement_->Execute(closure, context);
}

ClassDefinition::ClassDefinition(ObjectHolder cls)
    : cls_(std::move(cls)) {
}

ObjectHolder ClassDefinition::Execute(Closure& closure, [[maybe_unused]] Context& context) {
    runtime::Class* cls = cls_.TryAs<runtime::Class>();
    closure[cls->GetName()] = cls_;
    return ObjectHolder::None();
}

NewInstance::NewInstance(const runtime::Class& class_, std::vector<std::unique_ptr<Statement>> args)
    : class_instance_(class_), args_(std::move(args)) {
}

NewInstance::NewInstance(const runtime::Class& class_)
    : class_instance_(class_) {
}

ObjectHolder NewInstance::Execute(Closure& closure, Context& context) {
    if (class_instance_.HasMethod(INIT_METHOD, args_.size())) {
        std::vector<runtime::ObjectHolder> actual_args;
        actual_args.reserve(args_.size());

        for (const auto& arg : args_) {
            actual_args.push_back(arg->Execute(closure, context));
        }

        class_instance_.Call(INIT_METHOD, actual_args, context);
    }
    return runtime::ObjectHolder::Share(class_instance_);
}

ObjectHolder Stringify::Execute(Closure& closure, Context& context) {
    auto object = argument_->Execute(closure, context);
    if (object) {
        std::ostringstream os;
        object->Print(os, context);
        return ObjectHolder::Own(runtime::String(os.str()));
    } else {
        return ObjectHolder::Own(runtime::String("None"s));
    }
}

ObjectHolder Add::Execute(Closure& closure, Context& context) {
    auto lhs_object = lhs_->Execute(closure, context);
    auto rhs_object = rhs_->Execute(closure, context);

    auto lhs_number = lhs_object.TryAs<runtime::Number>();
    auto rhs_number = rhs_object.TryAs<runtime::Number>();
    if (lhs_number && rhs_number) {
        return ObjectHolder::Own(runtime::Number(lhs_number->GetValue() + rhs_number->GetValue()));
    }

    auto lhs_string = lhs_object.TryAs<runtime::String>();
    auto rhs_string = rhs_object.TryAs<runtime::String>();
    if (lhs_string && rhs_string) {
        return ObjectHolder::Own(runtime::String(lhs_string->GetValue() + rhs_string->GetValue()));
    }

    auto lhs_class_instance = lhs_object.TryAs<runtime::ClassInstance>();
    if (lhs_class_instance) {
        return lhs_class_instance->Call(ADD_METHOD, {rhs_object}, context);
    }

    throw runtime_error("Error addition"s);
}

ObjectHolder Sub::Execute(Closure& closure, Context& context) {
    auto lhs_number = lhs_->Execute(closure, context).TryAs<runtime::Number>();
    auto rhs_number = rhs_->Execute(closure, context).TryAs<runtime::Number>();
    if (lhs_number && rhs_number) {
        return ObjectHolder::Own(runtime::Number(lhs_number->GetValue() - rhs_number->GetValue()));
    }

    throw runtime_error("Error subtration"s);
}

ObjectHolder Mult::Execute(Closure& closure, Context& context) {
    auto lhs_number = lhs_->Execute(closure, context).TryAs<runtime::Number>();
    auto rhs_number = rhs_->Execute(closure, context).TryAs<runtime::Number>();
    if (lhs_number && rhs_number) {
        return ObjectHolder::Own(runtime::Number(lhs_number->GetValue() * rhs_number->GetValue()));
    }

    throw runtime_error("Error multiplication"s);
}

ObjectHolder Div::Execute(Closure& closure, Context& context) {
    auto lhs_number = lhs_->Execute(closure, context).TryAs<runtime::Number>();
    auto rhs_number = rhs_->Execute(closure, context).TryAs<runtime::Number>();
    if (lhs_number && rhs_number) {
        if (rhs_number->GetValue() == 0) {
            throw runtime_error("Division by zero"s);
        }
        return ObjectHolder::Own(runtime::Number(lhs_number->GetValue() / rhs_number->GetValue()));
    }

    throw runtime_error("Error division"s);
}

ObjectHolder Or::Execute(Closure& closure, Context& context) {
    auto lhs_object = lhs_->Execute(closure, context);
    if (runtime::IsTrue(lhs_object)) {
        return ObjectHolder::Own(runtime::Bool(true));
    } else {
        auto rhs_object = rhs_->Execute(closure, context);
        return ObjectHolder::Own(runtime::Bool(runtime::IsTrue(rhs_object)));
    }
}

ObjectHolder And::Execute(Closure& closure, Context& context) {
    auto lhs_object = lhs_->Execute(closure, context);
    if (runtime::IsTrue(lhs_object)) {
        auto rhs_object = rhs_->Execute(closure, context);
        return ObjectHolder::Own(runtime::Bool(runtime::IsTrue(rhs_object)));
    } else {  
        return ObjectHolder::Own(runtime::Bool(false));
    }
}

ObjectHolder Not::Execute(Closure& closure, Context& context) {
    auto bool_object = !runtime::IsTrue(argument_->Execute(closure, context));
    return ObjectHolder::Own(runtime::Bool(bool_object));
}

Comparison::Comparison(Comparator cmp, unique_ptr<Statement> lhs, unique_ptr<Statement> rhs)
    : BinaryOperation(std::move(lhs), std::move(rhs)) {
    cmp_ = std::move(cmp);
}

ObjectHolder Comparison::Execute(Closure& closure, Context& context) {
    auto object_lhs = lhs_->Execute(closure, context);
    auto object_rhs = rhs_->Execute(closure, context);
    runtime::Bool result = cmp_(object_lhs, object_rhs, context);
    return runtime::ObjectHolder::Own(std::move(result));
}

ObjectHolder Compound::Execute(Closure& closure, Context& context) {
    for (const auto& arg : args_) {
        arg->Execute(closure, context);
    }
    return ObjectHolder::None();
}

IfElse::IfElse(std::unique_ptr<Statement> condition, std::unique_ptr<Statement> if_body,
               std::unique_ptr<Statement> else_body)
    : condition_(std::move(condition)), if_body_(std::move(if_body)), else_body_(std::move(else_body)) {
}

ObjectHolder IfElse::Execute(Closure& closure, Context& context) {
    if (runtime::IsTrue(condition_->Execute(closure, context))) {
        return if_body_->Execute(closure, context);
    } else if (else_body_) {
        return else_body_->Execute(closure, context);
    }
    return runtime::ObjectHolder::None();
}

}  // namespace ast
