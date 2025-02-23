// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_COMPONENT_UPDATER_ORIGIN_TRIALS_COMPONENT_INSTALLER_H_
#define CHROME_BROWSER_COMPONENT_UPDATER_ORIGIN_TRIALS_COMPONENT_INSTALLER_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/values.h"
#include "base/version.h"
#include "components/component_updater/default_component_installer.h"

namespace component_updater {

class ComponentUpdateService;

class OriginTrialsComponentInstallerTraits : public ComponentInstallerTraits {
 public:
  OriginTrialsComponentInstallerTraits() = default;
  ~OriginTrialsComponentInstallerTraits() override = default;

 private:
  bool VerifyInstallation(const base::DictionaryValue& manifest,
                          const base::FilePath& install_dir) const override;
  bool CanAutoUpdate() const override;
  bool RequiresNetworkEncryption() const override;
  bool OnCustomInstall(const base::DictionaryValue& manifest,
                       const base::FilePath& install_dir) override;
  void ComponentReady(const base::Version& version,
                      const base::FilePath& install_dir,
                      std::unique_ptr<base::DictionaryValue> manifest) override;
  base::FilePath GetBaseDirectory() const override;
  void GetHash(std::vector<uint8_t>* hash) const override;
  std::string GetName() const override;
  std::string GetAp() const override;

  DISALLOW_COPY_AND_ASSIGN(OriginTrialsComponentInstallerTraits);
};

// Call once during startup to make the component update service aware of
// the origin trials update component.
void RegisterOriginTrialsComponent(ComponentUpdateService* cus,
                                   const base::FilePath& user_data_dir);

}  // namespace component_updater

#endif  // CHROME_BROWSER_COMPONENT_UPDATER_ORIGIN_TRIALS_COMPONENT_INSTALLER_H_
