<?php

namespace Drupal\entity_overlay\Plugin;

/**
 * Defines an interface for entity overlay dialogs.
 */
interface EntityOverlayDialogInterface {

  /**
   * Returns the dialog command object.
   *
   * @return \Drupal\Core\Ajax\CommandInterface
   *   returns the open dialog command
   */
  public function getDialogOpenCommand(
    $selector,
    $title,
    $content,
    array $dialog_options = [],
    $settings = NULL
  );

  /**
   * Returns the library to attach.
   *
   * @return string
   *   returns the library to attach
   */
  public function getLibrary();

}
