<?php

namespace Drupal\entity_overlay\Plugin\EntityOverlay;

use Drupal\entity_overlay\Plugin\EntityOverlayDialogInterface;
use Drupal\Core\Ajax\OpenModalDialogCommand;

/**
 * Core Drupal Dialog.
 *
 * @EntityOverlayDialog(
 *   id = "core_modal",
 *   label = @Translation("Core Modal"),
 * )
 */
class CoreModalDialog implements EntityOverlayDialogInterface {

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
    return new OpenModalDialogCommand($title, $content, $dialog_options,
      $settings);
  }

  /**
   * {@inheritdoc}
   */
  public function getLibrary() {
    return "core/drupal.dialog.ajax";
  }

}
