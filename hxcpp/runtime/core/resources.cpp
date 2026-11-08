#include <hxcpp.h>

#include <algorithm>
#include <string_view>

namespace {

const ::hx::Resource* findResource(const ::String& name) {
    const auto wanted = name.view();
    for (const ::hx::Resource* resource = ::hx::GetResources();
         resource != nullptr && resource->mData != nullptr; ++resource) {
        if (std::string_view(resource->mName) == wanted)
            return resource;
    }
    return nullptr;
}

} // namespace

::Array<::String> __hxcpp_resource_names() {
    auto result = ::Array_obj<::String>::__new();
    for (const ::hx::Resource* resource = ::hx::GetResources();
         resource != nullptr && resource->mData != nullptr; ++resource)
        result->push(::String(resource->mName));
    return result;
}

::String __hxcpp_resource_string(::String name) {
    const ::hx::Resource* resource = findResource(name);
    if (resource == nullptr)
        return nullptr;
    return ::String(std::string_view(
        reinterpret_cast<const char*>(resource->mData),
        static_cast<std::size_t>(std::max(resource->mDataLength, 0))));
}

::Array<unsigned char> __hxcpp_resource_bytes(::String name) {
    const ::hx::Resource* resource = findResource(name);
    if (resource == nullptr)
        return nullptr;
    return ::Array_obj<unsigned char>::fromData(
        resource->mData, std::max(resource->mDataLength, 0));
}
