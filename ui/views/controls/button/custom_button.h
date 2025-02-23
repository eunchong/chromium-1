// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_CONTROLS_BUTTON_CUSTOM_BUTTON_H_
#define UI_VIEWS_CONTROLS_BUTTON_CUSTOM_BUTTON_H_

#include <memory>

#include "base/macros.h"
#include "ui/events/event_constants.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/throb_animation.h"
#include "ui/views/animation/ink_drop_state.h"
#include "ui/views/controls/button/button.h"

namespace views {

class InkDropDelegate;

// A button with custom rendering. The base of ImageButton and LabelButton.
// Note that this type of button is not focusable by default and will not be
// part of the focus chain, unless in accessibility mode. Call
// Button::ConfigureDefaultFocus() to make it part of the focus chain.
class VIEWS_EXPORT CustomButton : public Button, public gfx::AnimationDelegate {
 public:
  // An enum describing the events on which a button should notify its listener.
  enum NotifyAction {
    NOTIFY_ON_PRESS,
    NOTIFY_ON_RELEASE,
  };

  // The menu button's class name.
  static const char kViewClassName[];

  static const CustomButton* AsCustomButton(const View* view);
  static CustomButton* AsCustomButton(View* view);

  // Paint an MD-style focus ring on the given canvas at the given bounds.
  static void PaintMdFocusRing(gfx::Canvas* canvas, View* view);

  ~CustomButton() override;

  // Get/sets the current display state of the button.
  ButtonState state() const { return state_; }
  void SetState(ButtonState state);

  // Starts throbbing. See HoverAnimation for a description of cycles_til_stop.
  void StartThrobbing(int cycles_til_stop);

  // Stops throbbing immediately.
  void StopThrobbing();

  // Set how long the hover animation will last for.
  void SetAnimationDuration(int duration);

  void set_triggerable_event_flags(int triggerable_event_flags) {
    triggerable_event_flags_ = triggerable_event_flags;
  }
  int triggerable_event_flags() const { return triggerable_event_flags_; }

  // Sets whether |RequestFocus| should be invoked on a mouse press. The default
  // is true.
  void set_request_focus_on_press(bool value) {
    request_focus_on_press_ = value;
  }
  bool request_focus_on_press() const { return request_focus_on_press_; }

  // See description above field.
  void set_animate_on_state_change(bool value) {
    animate_on_state_change_ = value;
  }

  // Sets the event on which the button should notify its listener.
  void set_notify_action(NotifyAction notify_action) {
    notify_action_ = notify_action;
  }

  void set_hide_ink_drop_when_showing_context_menu(
      bool hide_ink_drop_when_showing_context_menu) {
    hide_ink_drop_when_showing_context_menu_ =
        hide_ink_drop_when_showing_context_menu;
  }

  void set_ink_drop_base_color(SkColor color) { ink_drop_base_color_ = color; }

  void SetHotTracked(bool is_hot_tracked);
  bool IsHotTracked() const;

  // Overridden from View:
  void OnEnabledChanged() override;
  const char* GetClassName() const override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseCaptureLost() override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void OnMouseMoved(const ui::MouseEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  bool OnKeyReleased(const ui::KeyEvent& event) override;
  void OnGestureEvent(ui::GestureEvent* event) override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override;
  void ShowContextMenu(const gfx::Point& p,
                       ui::MenuSourceType source_type) override;
  void OnDragDone() override;
  void GetAccessibleState(ui::AXViewState* state) override;
  void VisibilityChanged(View* starting_from, bool is_visible) override;
  std::unique_ptr<InkDropHover> CreateInkDropHover() const override;
  SkColor GetInkDropBaseColor() const override;

  // Overridden from gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;

  InkDropDelegate* ink_drop_delegate() const { return ink_drop_delegate_; }

  // Overridden from View:
  void Layout() override;
  void ViewHierarchyChanged(
      const ViewHierarchyChangedDetails& details) override;
  void OnFocus() override;
  void OnBlur() override;

 protected:
  // Construct the Button with a Listener. See comment for Button's ctor.
  explicit CustomButton(ButtonListener* listener);

  // Invoked from SetState() when SetState() is passed a value that differs from
  // the current state. CustomButton's implementation of StateChanged() does
  // nothing; this method is provided for subclasses that wish to do something
  // on state changes.
  virtual void StateChanged();

  // Returns true if the event is one that can trigger notifying the listener.
  // This implementation returns true if the left mouse button is down.
  virtual bool IsTriggerableEvent(const ui::Event& event);

  // Returns true if the button should become pressed when the user
  // holds the mouse down over the button. For this implementation,
  // we simply return IsTriggerableEvent(event).
  virtual bool ShouldEnterPushedState(const ui::Event& event);

  // Returns true if hover effect should be visible.
  virtual bool ShouldShowInkDropHover() const;

  void set_has_ink_drop_action_on_click(bool has_ink_drop_action_on_click) {
    has_ink_drop_action_on_click_ = has_ink_drop_action_on_click;
  }

  // Returns true if the button should enter hovered state; that is, if the
  // mouse is over the button, and no other window has capture (which would
  // prevent the button from receiving MouseExited events and updating its
  // state). This does not take into account enabled state.
  bool ShouldEnterHoveredState();

  void set_ink_drop_delegate(InkDropDelegate* ink_drop_delegate) {
    ink_drop_delegate_ = ink_drop_delegate;
  }

  // When called, creates and uses |md_focus_ring_| instead of a focus painter.
  void UseMdFocusRing();

  // Overridden from Button:
  void NotifyClick(const ui::Event& event) override;
  void OnClickCanceled(const ui::Event& event) override;

  const gfx::ThrobAnimation& hover_animation() const {
    return hover_animation_;
  }

 private:
  ButtonState state_;

  gfx::ThrobAnimation hover_animation_;

  // Should we animate when the state changes? Defaults to true.
  bool animate_on_state_change_;

  // Is the hover animation running because StartThrob was invoked?
  bool is_throbbing_;

  // Mouse event flags which can trigger button actions.
  int triggerable_event_flags_;

  // See description above setter.
  bool request_focus_on_press_;

  // Animation delegate for the ink drop ripple effect. It is owned by a
  // descendant class and needs to be reset before an instance of the concrete
  // CustomButton is destroyed.
  InkDropDelegate* ink_drop_delegate_;

  // The event on which the button should notify its listener.
  NotifyAction notify_action_;

  // True when a button click should trigger an animation action on
  // |ink_drop_delegate_|.
  // TODO(bruthig): Use an InkDropAction enum and drop the flag.
  bool has_ink_drop_action_on_click_;

  // The animation action to trigger on the |ink_drop_delegate_| when the button
  // is clicked.
  InkDropState ink_drop_action_on_click_;

  // When true, the ink drop ripple and hover will be hidden prior to showing
  // the context menu.
  bool hide_ink_drop_when_showing_context_menu_;

  // The color of the ripple and hover.
  SkColor ink_drop_base_color_;

  // The MD-style focus ring. This is not done via a FocusPainter
  // because it needs to paint to a layer so it can extend beyond the bounds of
  // |this|.
  views::View* md_focus_ring_;

  DISALLOW_COPY_AND_ASSIGN(CustomButton);
};

}  // namespace views

#endif  // UI_VIEWS_CONTROLS_BUTTON_CUSTOM_BUTTON_H_
