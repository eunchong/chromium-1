// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/examples/button_example.h"

#include "base/strings/utf_string_conversions.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/image/image.h"
#include "ui/resources/grit/ui_resources.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/blue_button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/resources/grit/views_resources.h"
#include "ui/views/view.h"

using base::ASCIIToUTF16;

namespace {
const char kLabelButton[] = "Label Button";
const char kLongText[] = "Start of Really Really Really Really Really Really "
                         "Really Really Really Really Really Really Really "
                         "Really Really Really Really Really Long Button Text";
}  // namespace

namespace views {
namespace examples {

ButtonExample::ButtonExample() : ExampleBase("Button") {
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  icon_ = rb.GetImageNamed(IDR_CLOSE_H).ToImageSkia();
}

ButtonExample::~ButtonExample() {
}

void ButtonExample::CreateExampleView(View* container) {
  container->set_background(Background::CreateSolidBackground(SK_ColorWHITE));
  BoxLayout* layout = new BoxLayout(BoxLayout::kVertical, 10, 10, 10);
  layout->set_cross_axis_alignment(BoxLayout::CROSS_AXIS_ALIGNMENT_CENTER);
  container->SetLayoutManager(layout);

  label_button_ = new LabelButton(this, ASCIIToUTF16(kLabelButton));
  Button::ConfigureDefaultFocus(label_button_);
  container->AddChildView(label_button_);

  styled_button_ = new LabelButton(this, ASCIIToUTF16("Styled Button"));
  styled_button_->SetStyle(Button::STYLE_BUTTON);
  container->AddChildView(styled_button_);

  disabled_button_ = new LabelButton(this, ASCIIToUTF16("Disabled Button"));
  disabled_button_->SetStyle(Button::STYLE_BUTTON);
  disabled_button_->SetState(Button::STATE_DISABLED);
  container->AddChildView(disabled_button_);

  container->AddChildView(new BlueButton(this, ASCIIToUTF16("Blue Button")));

  container->AddChildView(MdTextButton::CreateMdButton(
      nullptr, base::ASCIIToUTF16("Material design")));
  MdTextButton* md_button = MdTextButton::CreateMdButton(
      nullptr, base::ASCIIToUTF16("Strong call to action"));
  md_button->SetCallToAction(MdTextButton::STRONG_CALL_TO_ACTION);
  container->AddChildView(md_button);
  md_button = MdTextButton::CreateMdButton(
      nullptr, base::ASCIIToUTF16("Weak call to action"));
  md_button->SetCallToAction(MdTextButton::WEAK_CALL_TO_ACTION);
  container->AddChildView(md_button);

  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  image_button_ = new ImageButton(this);
  Button::ConfigureDefaultFocus(image_button_);
  image_button_->SetImage(ImageButton::STATE_NORMAL,
                          rb.GetImageNamed(IDR_CLOSE).ToImageSkia());
  image_button_->SetImage(ImageButton::STATE_HOVERED,
                          rb.GetImageNamed(IDR_CLOSE_H).ToImageSkia());
  image_button_->SetImage(ImageButton::STATE_PRESSED,
                          rb.GetImageNamed(IDR_CLOSE_P).ToImageSkia());
  container->AddChildView(image_button_);
}

void ButtonExample::LabelButtonPressed(LabelButton* label_button,
                                       const ui::Event& event) {
  PrintStatus("Label Button Pressed! count: %d", ++count_);
  if (event.IsControlDown()) {
    if (event.IsShiftDown()) {
      label_button->SetText(ASCIIToUTF16(
          label_button->GetText().empty()
              ? kLongText
              : label_button->GetText().length() > 50 ? kLabelButton : ""));
    } else if (event.IsAltDown()) {
      label_button->SetImage(
          Button::STATE_NORMAL,
          label_button->GetImage(Button::STATE_NORMAL).isNull()
              ? *icon_
              : gfx::ImageSkia());
    } else {
      static int alignment = 0;
      label_button->SetHorizontalAlignment(
          static_cast<gfx::HorizontalAlignment>(++alignment % 3));
    }
  } else if (event.IsShiftDown()) {
    if (event.IsAltDown()) {
      // Toggle focusability.
      label_button_->IsAccessibilityFocusable()
          ? label_button_->SetFocusBehavior(View::FocusBehavior::NEVER)
          : Button::ConfigureDefaultFocus(label_button_);
    } else {
      label_button->SetStyle(static_cast<Button::ButtonStyle>(
          (label_button->style() + 1) % Button::STYLE_COUNT));
    }
  } else if (event.IsAltDown()) {
    label_button->SetIsDefault(!label_button->is_default());
  } else {
    label_button->SetMinSize(gfx::Size());
  }
  example_view()->GetLayoutManager()->Layout(example_view());
}

void ButtonExample::ButtonPressed(Button* sender, const ui::Event& event) {
  if (sender == label_button_)
    LabelButtonPressed(label_button_, event);
  else if (sender == styled_button_)
    LabelButtonPressed(styled_button_, event);
  else if (sender == disabled_button_)
    LabelButtonPressed(disabled_button_, event);
  else
    PrintStatus("Image Button Pressed! count: %d", ++count_);
}

}  // namespace examples
}  // namespace views
