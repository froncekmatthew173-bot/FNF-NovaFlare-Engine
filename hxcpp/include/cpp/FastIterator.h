#pragma once

namespace cpp {

class IteratorBase : public ::hx::Object {
public:
    static const ::hx::gc::TypeDescriptor __novaType;

    virtual bool hasNext() = 0;
    virtual ::Dynamic _dynamicNext() = 0;
};

template<class T>
class FastIterator_obj : public IteratorBase {
public:
    static const ::hx::gc::TypeDescriptor __novaType;

    virtual T next() = 0;
    ::Dynamic _dynamicNext() override { return ::hx::boxDynamic(next()); }
};

inline const ::hx::gc::TypeDescriptor IteratorBase::__novaType{
    0x4350504954455242ULL,
    "cpp.IteratorBase",
    nullptr,
    static_cast<std::uint32_t>(sizeof(IteratorBase)),
    static_cast<std::uint16_t>(alignof(IteratorBase)),
    0,
    ::hx::gc::TypeNone,
    0,
    nullptr,
    nullptr,
    nullptr,
};

template<class T>
inline const ::hx::gc::TypeDescriptor FastIterator_obj<T>::__novaType{
    0x4350504641535400ULL ^ static_cast<std::uint64_t>(sizeof(T)),
    "cpp.FastIterator",
    &IteratorBase::__novaType,
    static_cast<std::uint32_t>(sizeof(FastIterator_obj<T>)),
    static_cast<std::uint16_t>(alignof(FastIterator_obj<T>)),
    0,
    ::hx::gc::TypeNone,
    0,
    nullptr,
    nullptr,
    nullptr,
};

} // namespace cpp
