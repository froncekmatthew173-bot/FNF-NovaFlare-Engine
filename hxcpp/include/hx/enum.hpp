#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace hx {

[[nodiscard]] ::Array<Dynamic> emptyEnumArguments();

class EnumBase_obj : public Object {
public:
    static const ::hx::gc::TypeDescriptor descriptor;
    String _hx_tag;
    ::Array<Dynamic> _hx_args;
    int _hx_index = 0;

    [[nodiscard]] int _hx_getIndex() const noexcept { return _hx_index; }
    [[nodiscard]] String _hx_getTag() const { return _hx_tag; }
    [[nodiscard]] String __Tag() const { return _hx_tag; }
    [[nodiscard]] ::Array<Dynamic> _hx_getParameters() const {
        return _hx_args ? _hx_args : emptyEnumArguments();
    }
    [[nodiscard]] int _hx_getParamCount() const noexcept {
        return _hx_args ? _hx_args->length : 0;
    }
    [[nodiscard]] Dynamic _hx_getParamI(int index) {
        return checkedArgument(index);
    }
    [[nodiscard]] int _hx_getInt(int index) {
        return checkedArgument(index).Cast<int>();
    }
    [[nodiscard]] Dynamic _hx_getObject(int index) {
        return checkedArgument(index);
    }
    [[nodiscard]] String _hx_getString(int index) {
        return checkedArgument(index).Cast<String>();
    }
    [[nodiscard]] double _hx_getFloat(int index) {
        return checkedArgument(index).Cast<double>();
    }
    [[nodiscard]] bool _hx_getBool(int index) {
        return checkedArgument(index).Cast<bool>();
    }
    [[nodiscard]] std::int64_t _hx_getInt64(int index) {
        return checkedArgument(index).Cast<std::int64_t>();
    }

    [[nodiscard]] static bool __GetStatic(const ::String&,
                                          ::Dynamic&,
                                          ::hx::PropertyAccess) {
        return false;
    }
    [[nodiscard]] static int __FindIndex(::String) { return -1; }
    [[nodiscard]] static int __FindArgCount(::String) { return -1; }

    class InitResult final {
    public:
        explicit InitResult(EnumBase_obj* value) noexcept : value_(value) {}
        [[nodiscard]] EnumBase_obj* operator->() const noexcept {
            return value_;
        }
        template<class T>
        [[nodiscard]] operator ::hx::ObjectPtr<T>() const {
            return ::hx::ObjectPtr<T>(static_cast<T*>(value_));
        }
        [[nodiscard]] operator ::Dynamic() const {
            return ::Dynamic(value_);
        }
    private:
        EnumBase_obj* value_;
    };

    template<class T>
    InitResult _hx_init(int index, const T& value) {
        if (!_hx_args)
            throw std::logic_error("enum constructor has no argument storage");
        _hx_args->setCtx(nullptr, index, Dynamic(value));
        return InitResult(this);
    }

private:
    [[nodiscard]] Dynamic checkedArgument(int index) {
        if (!_hx_args || index < 0 || index >= _hx_args->length)
            throw std::out_of_range("Haxe enum argument index");
        return _hx_args->__get(index);
    }
};

inline void traceEnumBase(void* object, std::size_t,
                          ::hx::gc::TraceVisitor& visitor) {
    auto* value = static_cast<EnumBase_obj*>(object);
    visitor.visit(value->_hx_tag.mPtr);
    visitor.visit(value->_hx_args.mPtr);
}

inline const ::hx::gc::TypeDescriptor EnumBaseDescriptor{
    0x454e554d42415345ULL,
    "EnumBase",
    nullptr,
    sizeof(EnumBase_obj),
    alignof(EnumBase_obj),
    0,
    ::hx::gc::TypeContainsReferences | ::hx::gc::TypeEnum,
    0,
    nullptr,
    &traceEnumBase,
    nullptr,
};

inline const ::hx::gc::TypeDescriptor EnumBase_obj::descriptor =
    EnumBaseDescriptor;

template<class T>
inline const ::hx::gc::TypeDescriptor EnumDescriptor{
    0x454e554d54595045ULL ^ static_cast<std::uint64_t>(sizeof(T)),
    "HaxeEnum",
    &EnumBaseDescriptor,
    sizeof(T),
    alignof(T),
    0,
    ::hx::gc::TypeContainsReferences | ::hx::gc::TypeEnum,
    0,
    nullptr,
    &traceEnumBase,
    nullptr,
};

template<class T>
[[nodiscard]] inline const ::hx::gc::TypeDescriptor&
enumDescriptor() noexcept {
    return EnumDescriptor<T>;
}

template<class T>
[[nodiscard]] ::hx::ObjectPtr<T> CreateEnum(String tag, int index,
                                             int argumentCount) {
    ::hx::gc::RefSlot* tagRoots[]{&tag.mPtr};
    ::hx::gc::RootScope tagScope(tagRoots, 1);
    ::Array<Dynamic> arguments;
    ::hx::gc::RefSlot* argumentRoots[]{&arguments.mPtr};
    ::hx::gc::RootScope argumentScope(argumentRoots, 1);
    if (argumentCount > 0)
        arguments = ::Array_obj<Dynamic>::__new(argumentCount);

    auto result = ::hx::allocate<T>(EnumDescriptor<T>);
    result->_hx_tag = tag;
    result->_hx_args = arguments;
    result->_hx_index = index;
    return result;
}

template<class T>
[[nodiscard]] ::hx::ObjectPtr<T> CreateConstEnum(String tag, int index) {
    return CreateEnum<T>(tag, index, 0);
}

} // namespace hx
