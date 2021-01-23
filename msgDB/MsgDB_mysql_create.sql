CREATE TABLE `Dialog` (
	`dialog_id` INT NOT NULL AUTO_INCREMENT,
	`login1_id` INT NOT NULL,
	`login2_id` INT NOT NULL,
	`created_at` DATETIME NOT NULL,
	`updated_at` DATETIME NOT NULL,
	PRIMARY KEY (`dialog_id`)
);

CREATE TABLE `User` (
	`user_id` INT NOT NULL AUTO_INCREMENT,
	`login` varchar(20) NOT NULL UNIQUE,
	`password` varchar(20) NOT NULL,
	PRIMARY KEY (`user_id`)
);

CREATE TABLE `Message` (
	`message_id` INT NOT NULL AUTO_INCREMENT,
	`login_sender_id` INT NOT NULL,
	`login_recipient_id` INT NOT NULL,
	`date` DATETIME NOT NULL,
	`content` TEXT NOT NULL,
	`dialog_id` INT NOT NULL,
	`read_by_recipient` BOOLEAN NOT NULL,
	PRIMARY KEY (`message_id`)
);

CREATE TABLE `User/Dialog` (
	`user_to_dialog_id` INT NOT NULL AUTO_INCREMENT,
	`user_id` INT NOT NULL,
	`dialog_id` INT NOT NULL,
	PRIMARY KEY (`user_to_dialog_id`)
);

ALTER TABLE `Message` ADD CONSTRAINT `Message_fk0` FOREIGN KEY (`dialog_id`) REFERENCES `Dialog`(`dialog_id`);

ALTER TABLE `User/Dialog` ADD CONSTRAINT `User/Dialog_fk0` FOREIGN KEY (`user_id`) REFERENCES `User`(`user_id`);

ALTER TABLE `User/Dialog` ADD CONSTRAINT `User/Dialog_fk1` FOREIGN KEY (`dialog_id`) REFERENCES `Dialog`(`dialog_id`);

