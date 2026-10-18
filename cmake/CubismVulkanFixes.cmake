# Cubism Native 5 R5 Vulkan needs fixes for offscreen rendering. Apply small,
# checked substitutions to build-directory copies; never modify the user's SDK.
# See README.md for the scope of these fixes and validation instructions.
function(live2d_patch_vulkan_sources output)
    set(_directory "${_framework_root}/Rendering/Vulkan")
    set(_patched "${CMAKE_CURRENT_BINARY_DIR}/cubism-vulkan-patched")
    file(MAKE_DIRECTORY "${_patched}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_directory}/CubismRenderer_Vulkan.cpp" "${_directory}/CubismClass_Vulkan.cpp")
    file(READ "${_directory}/CubismRenderer_Vulkan.cpp" _renderer)
    set(_needle "    VkRenderingAttachmentInfoKHR depthStencilAttachment{};")
    string(FIND "${_renderer}" "${_needle}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "Unsupported Cubism Vulkan source; use Native 5 R5.")
    endif()
    # The Vulkan SDK bypasses Option.LoadFileFunction and opens SPIR-V from cwd.
    # Use the same embedded-resource callback as the OpenGL helper.
    string(FIND "${_renderer}" "    std::ifstream file(filename.GetRawString()" _load_begin)
    string(FIND "${_renderer}" "    VkShaderModule shaderModule;" _load_end)
    if(_load_begin EQUAL -1 OR _load_end EQUAL -1)
        message(FATAL_ERROR "Unsupported Cubism Vulkan shader loader.")
    endif()
    string(SUBSTRING "${_renderer}" 0 ${_load_begin} _before)
    string(SUBSTRING "${_renderer}" ${_load_end} -1 _after)
    set(_renderer "${_before}    csmSizeInt fileSize = 0;\n    csmByte* bytes = CubismFramework::GetLoadFileFunction()(filename.GetRawString(), &fileSize);\n    if (!bytes || fileSize <= 0 || fileSize % 4) { return VK_NULL_HANDLE; }\n    csmVector<csmUint32> buffer(fileSize / 4);\n    buffer.Resize(fileSize / 4);\n    memcpy(buffer.GetPtr(), bytes, fileSize);\n    CubismFramework::GetReleaseBytesFunction()(bytes);\n    VkShaderModuleCreateInfo createInfo{};\n    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;\n    createInfo.codeSize = fileSize;\n    createInfo.pCode = buffer.GetPtr();\n${_after}")
    string(REPLACE "${_needle}"
        "    _depthImage.SetImageLayout(drawCommandBuffer, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);\n\n${_needle}"
        _renderer "${_renderer}")
    string(REPLACE "depthStencilAttachment.clearValue = {1.0f, 0};"
        "depthStencilAttachment.clearValue.depthStencil = {1.0f, 0};" _renderer "${_renderer}")
    file(WRITE "${_patched}/CubismRenderer_Vulkan.cpp" "${_renderer}")
    file(READ "${_directory}/CubismClass_Vulkan.cpp" _image)
    # R5 accidentally overwrites the fragment destination with a ray tracing stage.
    string(REPLACE "        destinationStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;"
        "" _image "${_image}")
    # An anisotropy of 1 is ordinary sampling; no optional device feature needed.
    string(REPLACE "if (maxAnistropy >= 1.0f)" "if (maxAnistropy > 1.0f)" _image "${_image}")
    # Complete access scopes for SDK-created depth images.
    string(REPLACE "    switch (newLayout)\n    {"
        "    switch (newLayout)\n    {\n    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:\n        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;\n        break;"
        _image "${_image}")
    # Release views before their images, and handle partial creation consistently.
    set(_old "    if (image != VK_NULL_HANDLE)\n    {\n        vkDestroyImage(device, image, nullptr);\n        image = VK_NULL_HANDLE;\n    }\n    if (memory != VK_NULL_HANDLE)\n    {\n        vkDestroyImageView(device, view, nullptr);\n    }\n    if (view != VK_NULL_HANDLE)\n    {\n        vkFreeMemory(device, memory, nullptr);\n        view = VK_NULL_HANDLE;\n    }")
    set(_new "    if (view != VK_NULL_HANDLE)\n    {\n        vkDestroyImageView(device, view, nullptr);\n        view = VK_NULL_HANDLE;\n    }\n    if (image != VK_NULL_HANDLE)\n    {\n        vkDestroyImage(device, image, nullptr);\n        image = VK_NULL_HANDLE;\n    }\n    if (memory != VK_NULL_HANDLE)\n    {\n        vkFreeMemory(device, memory, nullptr);\n        memory = VK_NULL_HANDLE;\n    }\n    currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;")
    string(REPLACE "${_old}" "${_new}" _image "${_image}")
    file(WRITE "${_patched}/CubismClass_Vulkan.cpp" "${_image}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_directory}/CubismRenderTarget_Vulkan.cpp")
    file(READ "${_directory}/CubismRenderTarget_Vulkan.cpp" _target)
    string(REPLACE "depthStencilAttachment.clearValue = {1.0f, 0};"
        "depthStencilAttachment.clearValue.depthStencil = {1.0f, 0};" _target "${_target}")
    file(WRITE "${_patched}/CubismRenderTarget_Vulkan.cpp" "${_target}")
    set(${output} "${_patched}/CubismRenderer_Vulkan.cpp;${_patched}/CubismClass_Vulkan.cpp;${_patched}/CubismRenderTarget_Vulkan.cpp" PARENT_SCOPE)
endfunction()
