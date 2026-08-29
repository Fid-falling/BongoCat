function(bongo_cat_harden_cubism_user_model target)
  set(model_dir "${CUBISM_FRAMEWORK_PATH}/src/Model")
  set(source_path "${model_dir}/CubismUserModel.cpp")
  set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/cubism-model")
  set(output_source "${output_dir}/CubismUserModel.cpp")

  file(READ "${source_path}" source)
  string(REPLACE "\r\n" "\n" source "${source}")

  set(resize_unsafe [=[    _renderer->SetRenderTargetSize(width, height);]=])
  set(resize_safe [=[    if (_renderer)
    {
        _renderer->SetRenderTargetSize(width, height);
    }]=])
  if(source MATCHES "void CubismUserModel::SetRenderTargetSize")
    string(FIND "${source}" "${resize_safe}" resize_safe_position)
    if(resize_safe_position EQUAL -1)
      string(FIND "${source}" "${resize_unsafe}" resize_unsafe_position)
      if(resize_unsafe_position EQUAL -1)
        message(FATAL_ERROR
          "Cubism 5 r.5 user-model patch mismatch: render target resize")
      endif()
      string(REPLACE "${resize_unsafe}" "${resize_safe}" source "${source}")
    endif()
  else()
    message(FATAL_ERROR
      "Cubism 5 r.5 user-model patch mismatch: resize function")
  endif()

  set(create_unsafe [=[    _renderer = Rendering::CubismRenderer::Create(width, height);
    _renderer->Initialize(_model, maskBufferCount);]=])
  set(create_safe [=[    _renderer = Rendering::CubismRenderer::Create(width, height);

    if (_renderer)
    {
        _renderer->Initialize(_model, maskBufferCount);
    }]=])
  string(FIND "${source}" "${create_safe}" create_safe_position)
  if(create_safe_position EQUAL -1)
    string(FIND "${source}" "${create_unsafe}" create_unsafe_position)
    if(create_unsafe_position EQUAL -1)
      message(FATAL_ERROR
        "Cubism 5 r.5 user-model patch mismatch: renderer creation")
    endif()
    string(REPLACE "${create_unsafe}" "${create_safe}" source "${source}")
  endif()

  file(MAKE_DIRECTORY "${output_dir}")
  file(WRITE "${output_source}" "${source}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${source_path}")
  get_target_property(framework_sources ${target} SOURCES)
  list(REMOVE_ITEM framework_sources "${source_path}")
  set_property(TARGET ${target} PROPERTY SOURCES "${framework_sources}")
  target_sources(${target} PRIVATE "${output_source}")
  target_include_directories(${target} PRIVATE "${model_dir}")
endfunction()
