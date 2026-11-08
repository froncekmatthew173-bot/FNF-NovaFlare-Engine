#include <hxcpp.h>

#include <cstddef>
#include <cstdint>
#include <iostream>

class Node_obj final : public ::hx::Object {
public:
    ::hx::ObjectPtr<Node_obj> next;
    int value = 0;

    explicit Node_obj(int inValue) : value(inValue) {}

    void setNext(const ::hx::ObjectPtr<Node_obj>& target) {
        ::hx::store(this, next.mPtr, target.get());
    }

    static const std::uint32_t referenceOffsets[1];
    static const ::hx::gc::TypeDescriptor descriptor;

    static ::hx::ObjectPtr<Node_obj> create(int value) {
        return ::hx::allocate<Node_obj>(descriptor, value);
    }
};

class EmptyEnum_obj final : public ::hx::EnumBase_obj {};

const std::uint32_t Node_obj::referenceOffsets[1]{
    static_cast<std::uint32_t>(offsetof(Node_obj, next) +
                               offsetof(::hx::ObjectPtr<Node_obj>, mPtr)),
};

const ::hx::gc::TypeDescriptor Node_obj::descriptor{
    0x47454e4142494e56ULL, "Node", nullptr,
    sizeof(Node_obj), alignof(Node_obj), 0,
    ::hx::gc::TypeContainsReferences, 1, Node_obj::referenceOffsets,
    nullptr, nullptr,
};

int main() {
    __hxcpp_nova_initialize();
    (void)::hx::ClassOf<::hx::Class>();
    {
        HX_NOVAGC_VARI(::hx::ObjectPtr<EmptyEnum_obj>, value) =
            ::hx::CreateConstEnum<EmptyEnum_obj>(::String("Empty"), 0);
        HX_NOVAGC_VARI(::Array<::Dynamic>, parameters) =
            value->_hx_getParameters();
        if (!parameters || parameters->length != 0) {
            std::cerr << "parameterless enum returned null parameters\n";
            return 1;
        }
        if (::Dynamic(value).toString() != "Empty") {
            std::cerr << "parameterless enum did not stringify to its tag\n";
            return 1;
        }
    }
    {
        HX_NOVAGC_VARI(::hx::ObjectPtr<EmptyEnum_obj>, value) =
            ::hx::CreateEnum<EmptyEnum_obj>(::String("Some"), 1, 1);
        value->_hx_init(0, 7);
        if (::Dynamic(value).toString() != "Some(7)") {
            std::cerr << "enum arguments were not included in toString\n";
            return 1;
        }
    }
    try {
        (void)__hxcpp_new_date(0, 0, 0, 0, 0, 0);
        (void)__hxcpp_utc_date(0, 0, 0, 0, 0, 0);
    } catch (...) {
        std::cerr << "Date.fromTime temporary date construction threw\n";
        return 1;
    }
    {
        ::hx::ObjectPtr<Node_obj> nullNode;
        bool caught = false;
        try {
            (void)nullNode->value;
        } catch (const ::Dynamic& error) {
            caught = error.Cast<::String>() == "Null Object Reference";
        }
        if (!caught) {
            std::cerr << "ObjectPtr null dereference was not a Haxe exception\n";
            return 1;
        }
    }
    {
        HX_NOVAGC_VARI(::hx::ObjectPtr<Node_obj>, node) = Node_obj::create(3);
        bool caught = false;
        try {
            (void)::hx::interface_check(node, 0x12345678);
        } catch (const ::Dynamic& error) {
            caught = error.Cast<::String>() == "Invalid Cast";
        }
        if (!caught) {
            std::cerr << "invalid interface cast was not a Haxe exception\n";
            return 2;
        }
    }
    {
        HX_NOVAGC_VARI(::hx::ObjectPtr<Node_obj>, root) = Node_obj::create(1);
        root->setNext(Node_obj::create(2));
        (void)Node_obj::create(99);

        __hxcpp_collect(true);
        __hxcpp_gc_compact();
        if (__hxcpp_gc_last_relocated_objects() < 2 ||
            root->value != 1 || root->next->value != 2) {
            std::cerr << "generated hxcpp ABI graph failed after relocation\n";
            return 1;
        }
    }
    __hxcpp_nova_shutdown();
    std::cout << "generated hxcpp ObjectPtr ABI test passed\n";
    return 0;
}
