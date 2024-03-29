<?php

namespace Drupal\entity_overlay\Plugin\Field\FieldFormatter;

use Drupal\Core\Entity\EntityInterface;
use Drupal\Core\Field\EntityReferenceFieldItemList;
use Drupal\Core\Field\FieldItemListInterface;
use Drupal\Core\Field\Plugin\Field\FieldFormatter\EntityReferenceEntityFormatter;
use Drupal\Core\Form\FormStateInterface;

/**
 * Plugin implementation of the 'entity reference label' formatter.
 *
 * @FieldFormatter(
 *   id = "entity_reference_label_entity_overlay_formatter",
 *   label = @Translation("Label overlay"),
 *   description = @Translation("Display the label of the referenced entities
 *   and display the rendered entity on click as an overlay."), field_types = {
 *     "entity_reference"
 *   }
 * )
 */
class EntityReferenceLabelOverlayFormatter extends EntityReferenceEntityFormatter {

  use EntityOverlayFormatterBase;

  /**
   * {@inheritdoc}
   */
  public static function defaultSettings() {
    return [
      'dialog_type' => 'core_dialog',
      'link' => TRUE,
    ] + parent::defaultSettings();
  }

  /**
   * {@inheritdoc}
   */
  public function settingsForm(array $form, FormStateInterface $form_state) {
    // @todo add dialog settings depending on the dialog type.
    $manager = \Drupal::service('plugin.manager.entity_overlay');
    $plugin_definitions = $manager->getDefinitions();
    $plugin_options = [];
    foreach ($plugin_definitions as $plugin_definition) {
      $plugin_options[$plugin_definition['id']] = $plugin_definition['label'];
    }
    if (!empty($plugin_definitions)) {
      $elements['dialog_type'] = [
        '#type' => 'select',
        '#title' => t('Dialog'),
        '#default_value' => $this->getSetting('dialog_type'),
        '#options' => $plugin_options,
        '#description' => t('Select which dialog type you would like.'),
      ];
    }
    $elements['view_mode'] = [
      '#type' => 'select',
      '#options' => $this->entityDisplayRepository->getViewModeOptions($this->getFieldSetting('target_type')),
      '#title' => t('View mode'),
      '#default_value' => $this->getSetting('view_mode'),
      '#required' => TRUE,
    ];

    return $elements;
  }

  /**
   * {@inheritdoc}
   */
  public function settingsSummary() {
    $summary = [];
    $manager = \Drupal::service('plugin.manager.entity_overlay');
    $plugin_definitions = $manager->getDefinitions();
    $plugin_options = [];
    foreach ($plugin_definitions as $plugin_definition) {
      $plugin_options[$plugin_definition['id']] = $plugin_definition['label'];
    }
    $view_modes = $this->entityDisplayRepository->getViewModeOptions($this->getFieldSetting('target_type'));
    $view_mode = $this->getSetting('view_mode');
    $summary[] = t('Rendered as @mode',
      ['@mode' => isset($view_modes[$view_mode]) ? $view_modes[$view_mode] : $view_mode]);
    $summary[] = t('Dialog opened as @dialog_type',
      ['@dialog_type' => $plugin_options[$this->getSetting('dialog_type')]]);
    return $summary;
  }

  /**
   * {@inheritdoc}
   */
  public function viewElements(FieldItemListInterface $items, $langcode) {
    $elements = [];
    $view_mode = $this->getSetting('view_mode');
    $dialog_type = $this->getSetting('dialog_type');
    if ($items instanceof EntityReferenceFieldItemList) {
      foreach ($this->getEntitiesToView($items, $langcode) as $delta => $entity) {
        if (!$entity->isNew()) {
          $elements[$delta] = $this->getOverlayLink($entity, $view_mode,
            $dialog_type);

          if (!empty($items[$delta]->_attributes)) {
            $elements[$delta]['#options'] += ['attributes' => []];
            $elements[$delta]['#options']['attributes'] += $items[$delta]->_attributes;
            // Unset field item attributes since they have been included in the
            // formatter output and shouldn't be rendered in the field template.
            unset($items[$delta]->_attributes);
          }
        }
        else {
          continue;
        }
        $elements[$delta]['#cache']['tags'] = $entity->getCacheTags();
      }
    }

    // Container for loading entity content.
    $elements[] = [
      '#type' => 'container',
      '#attributes' => [
        'id' => 'entity-overlay__container',
      ],
    ];

    $elements['#attached']['library'][] = 'core/drupal.ajax';
    return $elements;
  }

  /**
   * {@inheritdoc}
   */
  protected function checkAccess(EntityInterface $entity) {
    return $entity->access('view label', NULL, TRUE);
  }

}
