set(BONGO_CAT_RUNTIME_SOURCES
  src/core/app_state.c
  src/media/audio.c
  src/media/image.c src/media/image_resize.c src/media/image_alpha.c
  src/media/stb_image_impl.c
  src/platform/memory.c src/render/gl_api.c
  src/render/overlay.c src/render/overlay_draw.c
  src/render/mver_pointer_overlay.c src/render/mver_pointer_overlay_draw.c
  src/runtime/app.c src/runtime/config_store.c
  src/runtime/assets.c src/runtime/startup.c
  src/runtime/frame_audit.c src/runtime/frame_presentation_audit.c
  src/runtime/frame_clock.c src/runtime/modal_frame.c
  src/runtime/gamepad.c
  src/runtime/live2d_audit.c src/runtime/live2d_visual_audit.c
  src/runtime/live2d_viewer_audit.c
  src/runtime/memory_policy.c src/runtime/mouse.c src/runtime/mouse_mapping.c
  src/runtime/model_files.c src/runtime/model_catalog_runtime.c
  src/runtime/model_update.c src/runtime/model_storage.c
  src/runtime/model_import.c src/runtime/model_import_package.c
  src/runtime/model_import_discovery.c src/runtime/model_import_format.c
  src/runtime/model_import_portable.c src/runtime/model_import_portable_scan.c
  src/runtime/model_import_portable_identity.c
  src/runtime/model_import_portable_migration.c
  src/runtime/model_import_portable_signature.c
  src/runtime/model_import_mver_patch.c
  src/runtime/model_import_mver.c src/runtime/model_import_mver_image.c
  src/runtime/model_import_mver_shortcut.c src/runtime/model_import_mver_effect.c
  src/runtime/model_import_mver_motion.c src/runtime/model_import_mver_labels.c
  src/runtime/model_import_report.c src/runtime/model_import_mver_metadata.c
  src/runtime/model_import_metadata.c src/runtime/runtime_flow.c
  src/runtime/shortcuts.c src/runtime/tray.c
  src/runtime/window.c src/runtime/window_background.c
  src/runtime/window_display.c src/runtime/window_drag.c
  src/runtime/window_geometry.c src/runtime/window_menu_actions.c
  src/runtime/window_menu_behavior.c src/runtime/window_menu_preview.c
  src/runtime/window_hit.c src/runtime/window_wheel.c
  src/ui/nuklear_impl.c
  src/ui/ui_backend.c src/ui/ui_tooltip.c src/ui/ui_resize_cache.c
  src/ui/ui_scale.c
  src/ui/ui_cursor.c src/ui/ui_animation.c src/ui/ui_paint.c
  src/ui/ui_paint_cache.c src/ui/ui_paint_border.c src/ui/ui_paint_shell.c
  src/ui/ui_font.c src/ui/ui_font_atlas.c src/ui/ui_font_atlas_sources.c
  src/ui/ui_font_atlas_ranges.c src/ui/ui_font_atlas_upload.c
  src/ui/ui_font_reload.c src/ui/ui_input.c
  src/ui/ui_catime.c src/ui/ui_catime_tabs.c src/ui/ui_catime_theme.c
  src/ui/ui_catime_icons.c src/ui/ui_native_theme.c
  src/ui/preferences.c src/ui/preferences_scale.c src/ui/preferences_fonts.c
  src/ui/preferences_render.c src/ui/preferences_language.c
  src/ui/preferences_assets.c src/ui/preferences_about.c
  src/ui/preferences_about_community.c src/ui/preferences_live_resize.c
  src/ui/preferences_dialog.c src/ui/preferences_import.c
  src/ui/preferences_overlay.c src/ui/preferences_behavior_dialog.c
  src/ui/preferences_behavior_rename.c src/ui/preferences_behavior_row.c
  src/ui/preferences_scrollbar.c src/ui/preferences_text_edit.c
  src/ui/preferences_text_session.c src/ui/preferences_notice.c
  src/ui/preferences_model.c src/ui/preferences_model_rename.c
  src/ui/preferences_model_cover.c src/ui/preferences_model_glyphs.c
  src/ui/preferences_pages.c src/ui/preferences_theme.c
  src/ui/preferences_shortcuts.c src/ui/preferences_shortcut_clear.c
  src/ui/preferences_smoke.c src/ui/preferences_widgets.c
  src/ui/preferences_controls.c src/ui/preferences_combo.c
  src/ui/preferences_toggle.c)
