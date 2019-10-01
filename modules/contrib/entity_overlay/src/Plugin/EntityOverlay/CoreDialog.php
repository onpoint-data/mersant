<?php

namespace Drupal\entity_overlay\Plugin\EntityOverlay;

use Drupal\entity_overlay\Plugin\EntityOverlayDialogInterface;
use Drupal\Core\Ajax\OpenDialogCommand;

/**
 * Core Drupal Dialog.
 *
 * @EntityOverlayDialog(
 *   id = "core_dialog",
 *   label = @Translation("Core Dialog"),
 * )
 */
class CoreDialog implements EntityOverlayDialogInterface {

  /**
   * {@inheritdoc}
   */
  public function getDialogOpenCommand(
    $selector,
    $title,
    $content,
    array $dialog_options = [],
    $settings = NULL
  ) {
    return new OpenDialogCommand($selector, $title, $content, $dialog_options,
      $settings);
  }

  /**
   * {@inheritdoc}
   */
  public function getLibrary() {
    return "core/drupal.dialog.ajax";
  }

}
